#include "packaging/BindingChecker.h"
#include <unordered_set>
#include <sstream>

using json = nlohmann::ordered_json;

// ---------- helpers ----------
static bool contains_name(const json& arr, const std::string& name) {
    for (auto& it : arr) {
        if (it.contains("name") && it["name"].get<std::string>() == name) return true;
    }
    return false;
}

static json find_tensor(const json& arr, const std::string& name) {
    for (auto& it : arr) {
        if (it["name"].get<std::string>() == name) return it;
    }
    return json(); // null
}

static size_t elem_count(const std::vector<int64_t>& shape) {
    if (shape.empty()) return 1;
    size_t n = 1;
    for (auto d : shape) {
        if (d <= 0) d = 1;
        n *= static_cast<size_t>(d);
    }
    return n;
}

static std::string type_to_precision_guess(const std::string& dtype) {
    // dtype already normalized: float32/float64/int32/int64/bool/string
    if (dtype == "float32") return "float32";
    if (dtype == "float64") return "float64";
    if (dtype == "int32") return "int32";
    if (dtype == "int64") return "int64";
    if (dtype == "bool") return "bool";
    if (dtype == "string") return "string";
    return dtype;
}

static void push_warning(json& warnings, const std::string& code, const std::string& severity,
    const std::string& path, const std::string& message) {
    json w;
    w["code"] = code;
    w["severity"] = severity;
    w["path"] = path;
    w["message"] = message;
    warnings.push_back(w);
}

// ---------- main checks ----------
BindingCheckOutputs BindingChecker::runChecks(const PackagingTaskRequestLite& req, const json& detectedMeta) {
    BindingCheckOutputs out;
    out.mappingTable = json::array();
    out.unmatchedItems = json::array();
    out.warnings = json::array();
    out.compatibilityChecks = json::array();

    const auto& inputs = detectedMeta["inputs"];
    const auto& outputs = detectedMeta["outputs"];

    for (const auto& b : req.expectedBindings) {
        const bool isInput = (b.target.role == "input");
        const bool isOutput = (b.target.role == "output");

        json targetMeta;
        std::string actualDirection = b.target.role;
        if (isInput) targetMeta = find_tensor(inputs, b.target.name);
        else if (isOutput) targetMeta = find_tensor(outputs, b.target.name);

        // ---------- unmatched target ----------
        if (targetMeta.is_null() || targetMeta.empty()) {
            json um;
            um["kind"] = "SYSML_FIELD";
            um["sysmlPath"] = b.sysmlPath;
            um["reason"] = "No matching target tensor found: " + b.target.name;
            out.unmatchedItems.push_back(um);
            push_warning(out.warnings,
                "TARGET_TENSOR_NOT_FOUND",
                "ERROR",
                b.sysmlPath,
                "No matching target tensor found: " + b.target.name);

            // still produce a compatibility entry (all FAIL)
            json chk;
            chk["sysmlPath"] = b.sysmlPath;
            chk["targetName"] = b.target.name;
            chk["targetIndex"] = b.target.index.empty() ? json(nullptr) : json(b.target.index);
            chk["expectedDirection"] = b.expectedDirection;
            chk["actualDirection"] = actualDirection;

            chk["expectedType"] = b.expectedType;
            chk["actualType"] = nullptr;
            chk["expectedShape"] = b.expectedShape.empty() ? json(nullptr) : json(b.expectedShape);
            chk["actualShape"] = nullptr;
            chk["expectedPrecision"] = b.expectedPrecision;
            chk["actualPrecision"] = nullptr;

            chk["directionResult"] = "FAIL";
            chk["typeResult"] = "FAIL";
            chk["shapeResult"] = "FAIL";
            chk["precisionResult"] = "FAIL";
            chk["messages"] = json::array({ "Target tensor not found" });
            out.compatibilityChecks.push_back(chk);
            continue;
        }

        // ---------- matched: mapping table ----------
        {
            json m;
            m["sysmlPath"] = b.sysmlPath;
            m["targetName"] = b.target.name;
            m["targetKind"] = b.target.kind;
            m["targetRole"] = b.target.role;
            m["targetIndex"] = b.target.index.empty() ? json(nullptr) : json(b.target.index);
            if (b.target.kind == "tensor_slice" && b.target.slice.has_value()) {
                json sliceObj;
                sliceObj["start"] = b.target.slice->start;
                sliceObj["length"] = b.target.slice->length;
                m["targetSlice"] = sliceObj;
            }
            else {
                m["targetSlice"] = nullptr;
            }
            m["matchStatus"] = "MATCHED";
            out.mappingTable.push_back(m);
        }

        const std::string actualType = targetMeta["dataType"].get<std::string>();
        const auto actualShape = targetMeta["shape"].get<std::vector<int64_t>>();
        const std::string actualPrecision = type_to_precision_guess(actualType);

        // ---------- compatibility check entry ----------
        json chk;
        chk["sysmlPath"] = b.sysmlPath;
        chk["targetName"] = b.target.name;
        chk["targetIndex"] = b.target.index.empty() ? json(nullptr) : json(b.target.index);
        chk["expectedDirection"] = b.expectedDirection;
        chk["actualDirection"] = actualDirection;

        chk["expectedType"] = b.expectedType;
        chk["actualType"] = actualType;

        chk["expectedShape"] = b.expectedShape.empty() ? json(nullptr) : json(b.expectedShape);
        chk["actualShape"] = actualShape.empty() ? json(nullptr) : json(actualShape);

        chk["expectedPrecision"] = b.expectedPrecision;
        chk["actualPrecision"] = actualPrecision;

        chk["messages"] = json::array(); // will be populated below

        // ---- direction check ----
        if (!b.expectedDirection.empty() && b.expectedDirection != actualDirection) {
            chk["directionResult"] = "FAIL";
            std::string msg = "Expected direction " + b.expectedDirection + " but actual is " + actualDirection;
            chk["messages"].push_back(msg);
            push_warning(out.warnings, "DIRECTION_MISMATCH", "WARN", b.sysmlPath, msg);
        }
        else {
            chk["directionResult"] = "PASS";
            chk["messages"].push_back("Direction matches (expected: " + b.expectedDirection + ")");
        }

        // ---- type check ----
        const std::string& expectedType = b.expectedType;
        if (!expectedType.empty()) {
            bool typeOk = true;
            if ((expectedType == "double" && (actualType == "float32" || actualType == "float64")) ||
                (expectedType == "int" && (actualType == "int32" || actualType == "int64")) ||
                (expectedType == "bool" && actualType == "bool") ||
                (expectedType == "string" && actualType == "string")) {
                typeOk = true;
            }
            else {
                typeOk = false;
            }

            if (!typeOk) {
                chk["typeResult"] = "WARN";
                std::string msg = "Expected type " + expectedType + " but actual type is " + actualType;
                chk["messages"].push_back(msg);
                push_warning(out.warnings, "TYPE_MISMATCH", "WARN", b.sysmlPath, msg);
            }
            else {
                chk["typeResult"] = "PASS";
                chk["messages"].push_back("Type compatible (expected: " + expectedType + ")");
            }
        }
        else {
            chk["typeResult"] = "PASS";
            chk["messages"].push_back("Type check skipped (expectedType not provided)");
        }

        // ---- precision check ----
        const std::string& expectedPrecision = b.expectedPrecision;
        if (!expectedPrecision.empty() && expectedPrecision != actualPrecision) {
            chk["precisionResult"] = "WARN";
            std::string msg = "Expected precision " + expectedPrecision + " but actual is " + actualPrecision;
            chk["messages"].push_back(msg);
            push_warning(out.warnings, "PRECISION_DOWNCAST", "WARN", b.sysmlPath, msg);
        }
        else {
            chk["precisionResult"] = "PASS";
            if (expectedPrecision.empty())
                chk["messages"].push_back("Precision check skipped (expectedPrecision not provided)");
            else
                chk["messages"].push_back("Precision matches (" + actualPrecision + ")");
        }

        // ---- shape check ----
        if (!b.expectedShape.empty() || b.target.kind == "tensor_slice") {
            if (b.target.kind == "tensor_element") {
                const size_t count = elem_count(actualShape);
                if (b.target.index.size() != 1) {
                    chk["shapeResult"] = "WARN";
                    std::string msg = "tensor_element requires a single index";
                    chk["messages"].push_back(msg);
                    push_warning(out.warnings, "INDEX_FORMAT", "WARN", b.sysmlPath, msg);
                }
                else {
                    int idx = b.target.index[0];
                    if (idx < 0 || static_cast<size_t>(idx) >= count) {
                        chk["shapeResult"] = "FAIL";
                        std::string msg = "Index out of range (index=" + std::to_string(idx) + ", tensor element count=" + std::to_string(count) + ")";
                        chk["messages"].push_back(msg);
                        push_warning(out.warnings, "INDEX_OUT_OF_RANGE", "WARN", b.sysmlPath, msg);
                    }
                    else {
                        const auto& exp = b.expectedShape;
                        bool shapeOk = exp.empty() || (exp.size() == 1 && exp[0] == 1);
                        if (!shapeOk) {
                            chk["shapeResult"] = "FAIL";
                            std::string msg = "tensor_element is a scalar, but expectedShape is not empty or [1]";
                            chk["messages"].push_back(msg);
                            push_warning(out.warnings, "ELEMENT_SHAPE_MISMATCH", "ERROR", b.sysmlPath, msg);
                        }
                        else {
                            chk["shapeResult"] = "PASS";
                            chk["messages"].push_back("Tensor element index valid, interpreted as scalar");
                        }
                    }
                }
            }
            else if (b.target.kind == "tensor_slice") {
                if (!b.target.slice.has_value()) {
                    chk["shapeResult"] = "FAIL";
                    std::string msg = "tensor_slice binding has no slice specification (start/length required)";
                    chk["messages"].push_back(msg);
                    chk["actualShape"] = nullptr;
                    push_warning(out.warnings, "SLICE_MISSING", "ERROR", b.sysmlPath, msg);
                }
                else if (b.target.slice->start < 0 || b.target.slice->length <= 0) {
                    chk["shapeResult"] = "FAIL";
                    std::string msg = "Slice parameters invalid: start must be >= 0 and length > 0";
                    chk["messages"].push_back(msg);
                    chk["actualShape"] = nullptr;
                    push_warning(out.warnings, "SLICE_PARAM_INVALID", "ERROR", b.sysmlPath, msg);
                }
                else if (actualShape.size() != 1) {
                    chk["shapeResult"] = "FAIL";
                    std::string msg = "tensor_slice (P0) only supports 1D tensors; actual rank is " + std::to_string(actualShape.size());
                    chk["messages"].push_back(msg);
                    chk["actualShape"] = actualShape.empty() ? json(nullptr) : json(actualShape);
                    push_warning(out.warnings, "SLICE_ONLY_SUPPORTS_1D", "ERROR", b.sysmlPath, msg);
                }
                else {
                    const int64_t dim0 = actualShape[0];
                    const int64_t start = b.target.slice->start;
                    const int64_t length = b.target.slice->length;

                    if (dim0 > 0 && (start + length) > dim0) {
                        chk["shapeResult"] = "FAIL";
                        std::string msg = "Slice [" + std::to_string(start) + ":" + std::to_string(start + length) + ") out of range (tensor dim=" + std::to_string(dim0) + ")";
                        chk["messages"].push_back(msg);
                        chk["actualShape"] = json::array({ dim0 });
                        push_warning(out.warnings, "SLICE_OUT_OF_RANGE", "ERROR", b.sysmlPath, msg);
                    }
                    else {
                        const std::vector<int64_t> sliceShape = { length };
                        chk["actualShape"] = sliceShape;
                        bool shapeMismatch = false;
                        if (!b.expectedShape.empty()) {
                            shapeMismatch = (b.expectedShape.size() != 1 || b.expectedShape[0] != length);
                        }
                        if (shapeMismatch) {
                            chk["shapeResult"] = "FAIL";
                            std::string msg = "tensor_slice expectedShape does not match slice length " + std::to_string(length);
                            chk["messages"].push_back(msg);
                            push_warning(out.warnings, "SLICE_SHAPE_MISMATCH", "ERROR", b.sysmlPath, msg);
                        }
                        else {
                            chk["shapeResult"] = "PASS";
                            chk["messages"].push_back("Slice bounds valid, shape matches");
                        }
                    }
                }
            }
            else {
                // full tensor shape check
                const auto& exp = b.expectedShape;
                bool same = (exp.size() == actualShape.size());
                if (same) {
                    for (size_t i = 0; i < exp.size(); ++i) {
                        if (exp[i] != actualShape[i]) { same = false; break; }
                    }
                }
                if (!same) {
                    chk["shapeResult"] = "WARN";
                    std::string msg = "Expected shape differs from actual shape";
                    chk["messages"].push_back(msg);
                    push_warning(out.warnings, "SHAPE_MISMATCH", "WARN", b.sysmlPath, msg);
                }
                else {
                    chk["shapeResult"] = "PASS";
                    chk["messages"].push_back("Shape matches");
                }
            }
        }
        else {
            chk["shapeResult"] = "PASS";
            chk["messages"].push_back("Shape check skipped (no expected shape provided)");
        }

        out.compatibilityChecks.push_back(chk);
    }

    return out;
}

#include "packaging/DetectedModelMetaBuilder.h"
#include "packaging/JsonHelpers.h"

using json = nlohmann::ordered_json;

static std::string normalize_dtype(const std::string& t) {
    // Normalise dtype casing: ModelParser emits Float32/Float64/Int32/String/Bool;
    // Python scripts emit float32/float64/int32/string/bool already.
    if (t == "Float32" || t == "float32") return "float32";
    if (t == "Float64" || t == "float64" || t == "Double" || t == "double") return "float64";
    if (t == "Float16" || t == "float16" || t == "Half" || t == "half") return "float16";
    if (t == "Int8"  || t == "int8")  return "int8";
    if (t == "Int16" || t == "int16") return "int16";
    if (t == "Int32" || t == "int32" || t == "Int" || t == "int") return "int32";
    if (t == "Int64" || t == "int64" || t == "Long" || t == "long") return "int64";
    if (t == "Bool"  || t == "bool")  return "bool";
    if (t == "String"|| t == "string")return "string";
    return t;
}

json DetectedModelMetaBuilder::buildFromModelParserMeta(const json& modelParserMeta, const std::string& framework) {
    json out;
    out["framework"] = framework;

    std::string modelName = "";
    if (modelParserMeta.contains("metaData") && modelParserMeta["metaData"].contains("modelName")) {
        modelName = modelParserMeta["metaData"]["modelName"].get<std::string>();
    }
    out["modelName"] = modelName;

    out["inputs"] = json::array();
    for (auto& in : modelParserMeta["inputs"]) {
        if (!in.contains("shape") || !in["shape"].is_array()) continue;   // 跳过异常项
        auto shape = pkg::json_to_shape_i64(in["shape"]);
        json item;
        item["name"] = in["name"];
        item["dataType"] = normalize_dtype(in["dataType"].get<std::string>());
        item["shape"] = shape;
        item["rank"] = pkg::rank_from_shape(shape);
        out["inputs"].push_back(item);
    }

    out["outputs"] = json::array();
    for (auto& o : modelParserMeta["outputs"]) {
        auto shape = pkg::json_to_shape_i64(o["shape"]);
        json item;
        item["name"] = o["name"];
        item["dataType"] = normalize_dtype(o["dataType"].get<std::string>());
        item["shape"] = shape;
        item["rank"] = pkg::rank_from_shape(shape);
        out["outputs"].push_back(item);
    }

    // layers optional; P0 skip graph nodes
    out["layers"] = json::array();
    return out;
}

json DetectedModelMetaBuilder::buildFromPythonScriptMeta(const json& pythonScriptJson,
    const std::string& framework,
    const std::string& modelName)
{
    json out;
    out["framework"] = framework;

    // Use modelName from script output if available, otherwise fall back to caller-supplied name
    std::string name = modelName;
    if (pythonScriptJson.contains("modelName") && pythonScriptJson["modelName"].is_string()) {
        std::string fromScript = pythonScriptJson["modelName"].get<std::string>();
        if (!fromScript.empty()) name = fromScript;
    }
    out["modelName"] = name;

    out["inputs"] = json::array();
    if (pythonScriptJson.contains("inputs") && pythonScriptJson["inputs"].is_array()) {
        for (auto& in : pythonScriptJson["inputs"]) {
            json item;
            item["name"] = in.value("name", "");
            item["dataType"] = normalize_dtype(in.value("dataType", "float32"));
            if (in.contains("shape") && in["shape"].is_array()) {
                auto shape = pkg::json_to_shape_i64(in["shape"]);
                item["shape"] = shape;
                item["rank"] = in.contains("rank") ? in["rank"].get<int>() : pkg::rank_from_shape(shape);
            } else {
                item["shape"] = json::array();
                item["rank"] = in.value("rank", 0);
            }
            out["inputs"].push_back(item);
        }
    }

    out["outputs"] = json::array();
    if (pythonScriptJson.contains("outputs") && pythonScriptJson["outputs"].is_array()) {
        for (auto& o : pythonScriptJson["outputs"]) {
            json item;
            item["name"] = o.value("name", "");
            item["dataType"] = normalize_dtype(o.value("dataType", "float32"));
            if (o.contains("shape") && o["shape"].is_array()) {
                auto shape = pkg::json_to_shape_i64(o["shape"]);
                item["shape"] = shape;
                item["rank"] = o.contains("rank") ? o["rank"].get<int>() : pkg::rank_from_shape(shape);
            } else {
                item["shape"] = json::array();
                item["rank"] = o.value("rank", 0);
            }
            out["outputs"].push_back(item);
        }
    }

    out["layers"] = json::array();
    return out;
}

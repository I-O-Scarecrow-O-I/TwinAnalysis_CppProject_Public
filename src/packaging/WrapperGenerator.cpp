#include "packaging/WrapperGenerator.h"
#include <filesystem>
#include <fstream>
#include <cctype>
#include <set>
#include <map>

//using json = nlohmann::ordered_json;

static void ensure_dir(const fs::path& p) {
    if (!fs::exists(p)) fs::create_directories(p);
}

static std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

// Convert protocol namespace like "twin.generated" to valid C++ namespace like "twin::generated".
static std::string to_cpp_namespace(std::string ns) {
    ns = trim(ns);
    if (ns.empty()) return "twin::generated";
    for (char& c : ns) if (c == '.' || c == '/') c = ':';

    std::string filtered;
    filtered.reserve(ns.size());
    for (char c : ns) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':') filtered.push_back(c);
    }
    ns = filtered;

    std::string out;
    out.reserve(ns.size() + 4);
    for (size_t i = 0; i < ns.size();) {
        if (ns[i] == ':') {
            size_t j = i;
            while (j < ns.size() && ns[j] == ':') j++;
            out.append("::");
            i = j;
        }
        else {
            out.push_back(ns[i]);
            i++;
        }
    }
    while (out.rfind("::", 0) == 0) out.erase(0, 2);
    while (out.size() >= 2 && out.compare(out.size() - 2, 2, "::") == 0) out.erase(out.size() - 2);
    if (out.empty()) out = "twin::generated";
    return out;
}

// ---------------------------------------------------------------------------
// Field info for strong-typed I/O code generation
// ---------------------------------------------------------------------------
struct FieldInfo {
    std::string fieldName;
    std::string tensorName;
    std::string cppType;        // "double","int","bool","std::string","std::vector<float>"
    std::string accessorKind;   // "string","real","int","bool","tensor","tensor_element"
    int tensorElementIndex = 0;
};

static FieldInfo buildFieldInfo(
    const std::string& portName,
    const std::string& fieldName,
    const std::string& tensorName,
    const std::vector<ExpectedBinding>& bindings)
{
    FieldInfo fi;
    fi.fieldName = fieldName;
    fi.tensorName = tensorName;

    std::string sysmlPath = portName + "." + fieldName;
    for (const auto& b : bindings) {
        if (b.sysmlPath != sysmlPath) continue;

        const std::string& kind = b.target.kind;
        const std::string& etype = b.expectedType;

        if (kind == "tensor" || kind == "tensor_slice") {
            if (etype == "string") {
                fi.cppType = "std::string";
                fi.accessorKind = "string";
            }
            else {
                fi.cppType = "std::vector<float>";
                fi.accessorKind = "tensor";
            }
        }
        else {
            if (etype == "string") {
                fi.cppType = "std::string";
                fi.accessorKind = "string";
            }
            else if (etype == "double") {
                fi.cppType = "double";
                fi.accessorKind = "real";
            }
            else if (etype == "int" || etype == "integer") {
                fi.cppType = "int";
                fi.accessorKind = "int";
            }
            else if (etype == "bool") {
                fi.cppType = "bool";
                fi.accessorKind = "bool";
            }
            else {
                fi.cppType = "double";
                fi.accessorKind = "real";
            }
            if (kind == "tensor_element" && !b.target.index.empty()) {
                fi.tensorElementIndex = b.target.index[0];
                fi.accessorKind = "tensor_element";
            }
        }
        return fi;
    }

    fi.cppType = "std::vector<float>";
    fi.accessorKind = "tensor";
    return fi;
}

static std::vector<FieldInfo> collectFields(
    const std::string& role,         // "input" or "output"
    const PortMappingMap& portMapping,
    const std::vector<ExpectedBinding>& bindings)
{
    std::vector<FieldInfo> result;
    std::set<std::string> seen;
    std::map<std::string, std::set<std::string>> portRoles;

    for (const auto& b : bindings) {
        auto dotPos = b.sysmlPath.find('.');
        if (dotPos == std::string::npos) continue;
        std::string portName = b.sysmlPath.substr(0, dotPos);
        if (!portName.empty() && !b.target.role.empty()) {
            portRoles[portName].insert(b.target.role);
        }
    }

    auto inferPortRole = [&](const std::string& portName) -> std::string {
        auto it = portRoles.find(portName);
        if (it != portRoles.end() && it->second.size() == 1) {
            return *(it->second.begin());
        }
        std::string lower = portName;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("input") != std::string::npos || lower.find("_in") != std::string::npos || lower == "in") return "input";
        if (lower.find("output") != std::string::npos || lower.find("_out") != std::string::npos || lower == "out") return "output";
        return "";
    };

    for (const auto& pmPair : portMapping) {
        const std::string& portName = pmPair.first;
        const std::string inferredRole = inferPortRole(portName);
        if (!inferredRole.empty() && inferredRole != role) continue;

        for (const auto& kv : pmPair.second) {
            const std::string& fieldName = kv.first;
            const std::string& tensorName = kv.second;
            FieldInfo fi = buildFieldInfo(portName, fieldName, tensorName, bindings);
            result.push_back(fi);
            seen.insert(portName + "." + fieldName);
        }
    }

    for (const auto& b : bindings) {
        if (b.target.role != role) continue;
        if (b.target.kind != "tensor_element") continue;
        if (seen.count(b.sysmlPath)) continue;
        auto dotPos = b.sysmlPath.find('.');
        if (dotPos == std::string::npos) continue;

        std::string fieldName = b.sysmlPath;
        auto dot = fieldName.rfind('.');
        if (dot != std::string::npos) fieldName = fieldName.substr(dot + 1);

        FieldInfo fi = buildFieldInfo(b.sysmlPath.substr(0, dotPos), fieldName, b.target.name, bindings);
        result.push_back(fi);
        seen.insert(b.sysmlPath);
    }

    return result;
}

GeneratedWrapperFiles WrapperGenerator::generateStub(const PackagingTaskRequestLite& req,
    const std::string& framework,
    const fs::path& incDir,
    const fs::path& srcDir,
    const std::string& cls,
    const std::string& nsCpp)
{
    // Stub runtime class name (framework-independent)
    const std::string rtCls = framework + "ModelRuntime";

    fs::path rtHeader = incDir / (rtCls + ".h");
    fs::path rtSource = srcDir / (rtCls + ".cpp");
    fs::path wrapperHeader = incDir / (cls + ".h");
    fs::path wrapperSource = srcDir / (cls + ".cpp");

    // -- Stub runtime header --
    {
        std::ofstream f(rtHeader.string(), std::ios::binary);
        f << "#pragma once\n"
          << "// WARNING: " << framework << " inference is NOT implemented in this stub runtime.\n"
          << "// This file is generated by AiModelPackagingCLI as a compilable placeholder.\n"
          << "// Implement inference by integrating the actual " << framework << " C++ runtime.\n"
          << "#include <string>\n"
          << "#include <vector>\n\n"
          << "class " << rtCls << " {\n"
          << "public:\n"
          << "    explicit " << rtCls << "(const std::string& modelPath) : m_modelPath(modelPath) {}\n"
          << "\n"
          << "    // Returns false: inference not implemented for " << framework << ".\n"
          << "    bool init() { return true; }\n"
          << "    bool run(double /*time*/, double /*stepSize*/) {\n"
          << "        m_lastError = \"" << framework << " inference not implemented in generated stub\";\n"
          << "        return false;\n"
          << "    }\n"
          << "    void terminate() {}\n"
          << "\n"
          << "    void setRealInput(const std::string& /*port*/, double /*v*/) {}\n"
          << "    void setIntInput(const std::string& /*port*/, int /*v*/) {}\n"
          << "    void setBoolInput(const std::string& /*port*/, bool /*v*/) {}\n"
          << "    void setStringInput(const std::string& /*port*/, const std::string& /*v*/) {}\n"
          << "    void setTensorInput(const std::string& /*port*/, const std::vector<float>& /*v*/) {}\n"
          << "\n"
          << "    double getRealOutput(const std::string& /*port*/) const { return 0.0; }\n"
          << "    int    getIntOutput(const std::string& /*port*/) const { return 0; }\n"
          << "    bool   getBoolOutput(const std::string& /*port*/) const { return false; }\n"
          << "    std::string getStringOutput(const std::string& /*port*/) const { return \"\"; }\n"
          << "    std::vector<float> getTensorOutputFloat32(const std::string& /*port*/) const { return {}; }\n"
          << "\n"
          << "    std::string lastError() const { return m_lastError; }\n"
          << "\n"
          << "private:\n"
          << "    std::string m_modelPath;\n"
          << "    std::string m_lastError;\n"
          << "};\n";
    }

    // -- Stub runtime source (minimal; just satisfies linker if split build is used) --
    {
        std::ofstream f(rtSource.string(), std::ios::binary);
        f << "// WARNING: " << framework << " inference is NOT implemented.\n"
          << "// See " << rtCls << ".h for documentation.\n"
          << "#include \"" << rtCls << ".h\"\n";
    }

    // -- Collect port fields for wrapper struct --
    std::vector<FieldInfo> inputFields = collectFields("input", req.portMapping, req.expectedBindings);
    std::vector<FieldInfo> outputFields = collectFields("output", req.portMapping, req.expectedBindings);

    std::string inputsStructBody;
    for (const auto& fi : inputFields) inputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (inputsStructBody.empty()) inputsStructBody = "    // no mapped input fields\n";

    std::string outputsStructBody;
    for (const auto& fi : outputFields) outputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (outputsStructBody.empty()) outputsStructBody = "    // no mapped output fields\n";

    // -- Stub wrapper header --
    {
        std::ofstream f(wrapperHeader.string(), std::ios::binary);
        f << "#pragma once\n"
          << "// WARNING: " << framework << " inference is NOT implemented in this generated wrapper stub.\n"
          << "// Integrate the actual " << framework << " C++ runtime to enable inference.\n"
          << "#include <string>\n"
          << "#include <vector>\n"
          << "#include <memory>\n"
          << "#include \"" << rtCls << ".h\"\n\n"
          << "namespace " << nsCpp << " {\n\n"
          << "struct Inputs {\n" << inputsStructBody << "};\n\n"
          << "struct Outputs {\n" << outputsStructBody << "};\n\n"
          << "class " << cls << " {\n"
          << "public:\n"
          << "    " << cls << "() = default;\n"
          << "    ~" << cls << "() { terminate(); }\n\n"
          << "    bool initialize(const std::string& modelPath);\n"
          << "    // WARNING: step() will always return false because " << framework << " inference is not implemented.\n"
          << "    bool step(const Inputs& in, Outputs& out, double time, double stepSize);\n"
          << "    void terminate();\n"
          << "    std::string lastError() const { return m_lastError; }\n\n"
          << "private:\n"
          << "    std::unique_ptr<" << rtCls << "> m_rt;\n"
          << "    std::string m_lastError;\n"
          << "};\n\n"
          << "} // namespace " << nsCpp << "\n";
    }

    // -- Stub wrapper source --
    {
        std::ofstream f(wrapperSource.string(), std::ios::binary);
        f << "// WARNING: " << framework << " inference is NOT implemented.\n"
          << "#include \"" << cls << ".h\"\n\n"
          << "namespace " << nsCpp << " {\n\n"
          << "bool " << cls << "::initialize(const std::string& modelPath) {\n"
          << "    m_rt = std::make_unique<" << rtCls << ">(modelPath);\n"
          << "    return m_rt->init();\n"
          << "}\n\n"
          << "bool " << cls << "::step(const Inputs& in, Outputs& out, double time, double stepSize) {\n"
          << "    (void)in; (void)out;\n"
          << "    if (!m_rt) { m_lastError = \"runtime not initialized\"; return false; }\n"
          << "    if (!m_rt->run(time, stepSize)) { m_lastError = m_rt->lastError(); return false; }\n"
          << "    return true;\n"
          << "}\n\n"
          << "void " << cls << "::terminate() {\n"
          << "    if (m_rt) { m_rt->terminate(); m_rt.reset(); }\n"
          << "}\n\n"
          << "} // namespace " << nsCpp << "\n";
    }

    GeneratedWrapperFiles files;
    files.headerPath = wrapperHeader.string();
    files.sourcePath = wrapperSource.string();
    files.runtimeHeaderPath = rtHeader.string();
    files.runtimeSourcePath = rtSource.string();
    return files;
}

// ==================== generateTorch() ====================
GeneratedWrapperFiles WrapperGenerator::generateTorch(
    const PackagingTaskRequestLite& req,
    const json& detectedMeta,
    const fs::path& incDir,
    const fs::path& srcDir,
    const std::string& cls,
    const std::string& nsCpp)
{
    using namespace std;
    namespace fs = std::filesystem;
    ensure_dir(incDir);
    ensure_dir(srcDir);

    const string rtCls = "TorchModelRuntime";
    fs::path rtHeader = incDir / (rtCls + ".h");
    fs::path rtSource = srcDir / (rtCls + ".cpp");
    fs::path wrapperHeader = incDir / (cls + ".h");
    fs::path wrapperSource = srcDir / (cls + ".cpp");

    // ---------- 从 detectedMeta 提取模型元数据 ----------
    vector<string> modelInputNames, modelOutputNames;
    vector<vector<int64_t>> modelInputShapes, modelOutputShapes;
    vector<string> modelInputTypes, modelOutputTypes;

    if (detectedMeta.contains("inputs") && detectedMeta["inputs"].is_array()) {
        for (const auto& jt : detectedMeta["inputs"]) {
            string name = jt.value("name", "");
            if (!name.empty()) modelInputNames.push_back(name);
            vector<int64_t> shape;
            if (jt.contains("shape") && jt["shape"].is_array()) {
                for (const auto& dim : jt["shape"])
                    if (dim.is_number_integer()) shape.push_back(dim.get<int64_t>());
            }
            modelInputShapes.push_back(shape);
            modelInputTypes.push_back(jt.value("dataType", "float32"));
        }
    }
    if (detectedMeta.contains("outputs") && detectedMeta["outputs"].is_array()) {
        for (const auto& jt : detectedMeta["outputs"]) {
            string name = jt.value("name", "");
            if (!name.empty()) modelOutputNames.push_back(name);
            vector<int64_t> shape;
            if (jt.contains("shape") && jt["shape"].is_array()) {
                for (const auto& dim : jt["shape"])
                    if (dim.is_number_integer()) shape.push_back(dim.get<int64_t>());
            }
            modelOutputShapes.push_back(shape);
            modelOutputTypes.push_back(jt.value("dataType", "float32"));
        }
    }

    // ---------- 收集输入/输出字段（用于 Inputs/Outputs 结构体）----------
    vector<FieldInfo> inputFields = collectFields("input", req.portMapping, req.expectedBindings);
    vector<FieldInfo> outputFields = collectFields("output", req.portMapping, req.expectedBindings);

    // ========== 生成 TorchModelRuntime.h ==========
    {
        ofstream f(rtHeader.string(), ios::binary);
        f << R"WRAPGENCODE(#pragma once
#include <torch/script.h>
#include <string>
#include <vector>
#include <memory>

class TorchModelRuntime {
public:
    TorchModelRuntime(const std::string& modelPath);
    ~TorchModelRuntime();

    bool init();
    bool run(const std::vector<torch::Tensor>& inputs,
             std::vector<torch::Tensor>& outputs);
    void terminate();
    std::string lastError() const { return m_lastError; }

private:
    std::string m_modelPath;
    std::shared_ptr<torch::jit::Module> m_module;
    std::string m_lastError;
};
)WRAPGENCODE";
    }

    // ========== 生成 TorchModelRuntime.cpp ==========
    {
        ofstream f(rtSource.string(), ios::binary);
        f << R"WRAPGENCODE(#include "TorchModelRuntime.h"
#include <filesystem>
#include <fstream>

TorchModelRuntime::TorchModelRuntime(const std::string& modelPath)
    : m_modelPath(modelPath) {}

TorchModelRuntime::~TorchModelRuntime() { terminate(); }

bool TorchModelRuntime::init() {
    try {
        std::filesystem::path p(m_modelPath);
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs.is_open()) {
            m_lastError = "Cannot open model file: " + m_modelPath;
            return false;
        }
        m_module = std::make_shared<torch::jit::Module>(torch::jit::load(ifs));
        m_module->eval();
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

bool TorchModelRuntime::run(const std::vector<torch::Tensor>& inputs,
                             std::vector<torch::Tensor>& outputs) {
    if (!m_module) {
        m_lastError = "Module not initialized";
        return false;
    }
    try {
        std::vector<c10::IValue> ivalue_inputs;
        ivalue_inputs.reserve(inputs.size());
        for (const auto& t : inputs)
            ivalue_inputs.emplace_back(t.contiguous());

        torch::jit::IValue result = m_module->forward(ivalue_inputs);

        if (result.isTuple()) {
            auto elems = result.toTuple()->elements();
            outputs.clear();
            for (auto& e : elems)
                outputs.push_back(e.toTensor().contiguous());
        } else if (result.isTensor()) {
            outputs = { result.toTensor().contiguous() };
        } else {
            m_lastError = "Unexpected model output type";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

void TorchModelRuntime::terminate() {
    m_module.reset();
}
)WRAPGENCODE";
    }

    // ========== 生成 Wrapper 头文件 ==========
    string inputsStructBody, outputsStructBody;
    for (const auto& fi : inputFields)
        inputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (inputsStructBody.empty()) inputsStructBody = "    // no mapped input fields\n";
    for (const auto& fi : outputFields)
        outputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (outputsStructBody.empty()) outputsStructBody = "    // no mapped output fields\n";

    {
        ofstream f(wrapperHeader.string(), ios::binary);
        f << R"(#pragma once
#include "TorchModelRuntime.h"
#include "IModelBlock.h"
#include <string>
#include <vector>
#include <memory>

namespace )" << nsCpp << R"( {

struct Inputs {
)" << inputsStructBody << R"(};

struct Outputs {
)" << outputsStructBody << R"(};

class )" << cls << R"( : public IModelBlock {
public:
    )" << cls << R"(() = default;
    ~)" << cls << R"(();

    // original lifecycle (kept for internal use)
    bool initialize(const std::string& modelPath);
    bool step(const Inputs& in, Outputs& out, double time, double stepSize);

    // IModelBlock overrides
    ModelStatus init() override;
    ModelStatus configure(const std::string& configData) override;
    ModelStatus step(double time, double stepSize) override;
    ModelStatus reset() override;
    ModelStatus terminate() override;

    void setRealInput(const std::string& portName, double value) override;
    double getRealOutput(const std::string& portName) const override;
    void setIntInput(const std::string& portName, int value) override;
    int getIntOutput(const std::string& portName) const override;
    void setBoolInput(const std::string& portName, bool value) override;
    bool getBoolOutput(const std::string& portName) const override;
    void setStringInput(const std::string& portName, const std::string& value) override;
    std::string getStringOutput(const std::string& portName) const override;
    void setTensorInput(const std::string& portName, const std::vector<float>& tensorData) override;

    std::string getBlockName() const override;
    std::string getLastError() const override;
    std::vector<std::string> getPortList() const override;

    // backward‑compatible shortcut
    std::string lastError() const { return m_lastError; })";

    f << R"(
private:
    std::unique_ptr<TorchModelRuntime> m_rt;
    std::string m_lastError;
)";
    f << "    std::string m_blockName = \"" << cls << "\";\n"
        << "    std::string m_modelPath;\n";
    f << R"(};

} // namespace )" << nsCpp << R"(
)";
    }

    // ========== 生成 Wrapper 源文件 ==========
    {
        ofstream f(wrapperSource.string(), ios::binary);
        // 开头固定部分
        f << R"WRAPGENCODE(#include ")WRAPGENCODE" << cls << R"WRAPGENCODE(.h"
#include <cstring>

namespace )WRAPGENCODE" << nsCpp << R"WRAPGENCODE( {

)WRAPGENCODE" << cls << R"WRAPGENCODE(::~)WRAPGENCODE" << cls << R"WRAPGENCODE(() { terminate(); }

// ---------- original initialization ----------
bool )WRAPGENCODE" << cls << R"WRAPGENCODE(::initialize(const std::string& modelPath) {
    m_rt.reset(new TorchModelRuntime(modelPath));
    if (!m_rt->init()) {
        m_lastError = m_rt->lastError();
        return false;
    }
    m_modelPath = modelPath;
    return true;
}

// ---------- original structured step ----------
bool )WRAPGENCODE" << cls << R"WRAPGENCODE(::step(const Inputs& in, Outputs& out, double time, double stepSize) {
    (void)time; (void)stepSize;
    if (!m_rt) { m_lastError = "runtime not initialized"; return false; }

    try {
        std::vector<torch::Tensor> inputs;
)WRAPGENCODE";

        // 为每个模型输入生成构造代码（动态拼接）
        for (size_t mi = 0; mi < modelInputNames.size(); ++mi) {
            const auto& shape = modelInputShapes[mi];
            const string& name = modelInputNames[mi];

            // 找到对应的 FieldInfo（包含类型信息）
            const FieldInfo* fiPtr = nullptr;
            for (const auto& fi : inputFields) {
                if (fi.tensorName == name) {
                    fiPtr = &fi;
                    break;
                }
            }
            if (!fiPtr) {
                f << "        m_lastError = \"No input field bound to " << name << "\"; return false;\n";
                f << "        return false;\n";
                break;
            }
            const FieldInfo& fi = *fiPtr;

            if (fi.accessorKind == "tensor") {
                bool hasDynamic = false;
                for (auto d : shape) if (d == -1) { hasDynamic = true; break; }
                f << "        {\n";
                f << "            const auto& data = in." << fi.fieldName << ";\n";
                if (hasDynamic) {
                    f << "            if (data.size() % 2 != 0) {\n";
                    f << "                m_lastError = \"Input data length must be even (IQ pairs)\";\n";
                    f << "                return false;\n";
                    f << "            }\n";
                    f << "            int64_t T = static_cast<int64_t>(data.size()) / 2;\n";
                    f << "            std::vector<int64_t> shape = {1, 2, T};\n";
                }
                else {
                    f << "            std::vector<int64_t> shape = {";
                    for (size_t d = 0; d < shape.size(); ++d) {
                        if (d > 0) f << ", ";
                        f << shape[d];
                    }
                    f << "};\n";
                }
                f << "            auto tensor = torch::from_blob(const_cast<float*>(data.data()), shape, torch::kFloat32).clone();\n";
                f << "            inputs.push_back(tensor);\n";
                f << "        }\n";
            }
            else if (fi.accessorKind == "real" || fi.accessorKind == "int" || fi.accessorKind == "bool") {
                f << "        {\n";
                f << "            auto value = static_cast<float>(in." << fi.fieldName << ");\n";
                f << "            auto tensor = torch::tensor(value, torch::kFloat32);\n";
                f << "            inputs.push_back(tensor.reshape({1}));\n";
                f << "        }\n";
            }
            else if (fi.accessorKind == "string") {
                f << "        {\n";
                f << "            m_lastError = \"String input not supported for PyTorch wrapper: " << fi.fieldName << "\";\n";
                f << "            return false;\n";
                f << "        }\n";
                break;
            }
            else {
                f << "        {\n";
                f << "            m_lastError = \"Unsupported input kind for " << fi.fieldName << ": " << fi.accessorKind << "\";\n";
                f << "            return false;\n";
                f << "        }\n";
                break;
            }
        }

        f << R"WRAPGENCODE(
        std::vector<torch::Tensor> outputs;
        if (!m_rt->run(inputs, outputs)) {
            m_lastError = m_rt->lastError();
            return false;
        }

        // Extract outputs by model order, map to fields
)WRAPGENCODE";

        for (size_t mi = 0; mi < modelOutputNames.size(); ++mi) {
            const string& name = modelOutputNames[mi];
            string fieldName;
            for (const auto& fi : outputFields) {
                if (fi.tensorName == name) { fieldName = fi.fieldName; break; }
            }
            if (fieldName.empty()) continue;

            f << "        if (" << mi << " < outputs.size()) {\n";
            f << "            auto& t = outputs[" << mi << "];\n";
            f << "            out." << fieldName << ".assign(t.data_ptr<float>(), t.data_ptr<float>() + t.numel());\n";
            f << "        }\n";
        }

        f << R"WRAPGENCODE(
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

// ========== IModelBlock overrides ==========
ModelStatus )WRAPGENCODE" << cls << R"WRAPGENCODE(::init() {
    // init is satisfied after a successful initialize; otherwise ERROR
    return ModelStatus::OK;
}

ModelStatus )WRAPGENCODE" << cls << R"WRAPGENCODE(::configure(const std::string& configData) {
    // configData = path to the .pt file
    if (initialize(configData))
        return ModelStatus::OK;
    return ModelStatus::ERROR;
}

ModelStatus )WRAPGENCODE" << cls << R"WRAPGENCODE(::step(double time, double stepSize) {
    (void)time; (void)stepSize;
    m_lastError = "Structured step (Inputs, Outputs) required for PyTorch model";
    return ModelStatus::ERROR;
}

ModelStatus )WRAPGENCODE" << cls << R"WRAPGENCODE(::reset() {
    terminate();
    if (!m_modelPath.empty()) {
        return configure(m_modelPath);
    }
    m_lastError = "No model path saved, cannot reset";
    return ModelStatus::ERROR;
}

ModelStatus )WRAPGENCODE" << cls << R"WRAPGENCODE(::terminate() {
    if (m_rt) {
        m_rt->terminate();
        m_rt.reset();
    }
    return ModelStatus::OK;
}

// ---------- scalar I/O (not supported) ----------
void )WRAPGENCODE" << cls << R"WRAPGENCODE(::setRealInput(const std::string& portName, double value) {
    m_lastError = "setRealInput not supported for PyTorch wrapper";
}
double )WRAPGENCODE" << cls << R"WRAPGENCODE(::getRealOutput(const std::string& portName) const {
    const_cast<std::string&>(m_lastError) = "getRealOutput not supported for PyTorch wrapper";
    return 0.0;
}
void )WRAPGENCODE" << cls << R"WRAPGENCODE(::setIntInput(const std::string& portName, int value) {
    m_lastError = "setIntInput not supported for PyTorch wrapper";
}
int )WRAPGENCODE" << cls << R"WRAPGENCODE(::getIntOutput(const std::string& portName) const {
    const_cast<std::string&>(m_lastError) = "getIntOutput not supported for PyTorch wrapper";
    return 0;
}
void )WRAPGENCODE" << cls << R"WRAPGENCODE(::setBoolInput(const std::string& portName, bool value) {
    m_lastError = "setBoolInput not supported for PyTorch wrapper";
}
bool )WRAPGENCODE" << cls << R"WRAPGENCODE(::getBoolOutput(const std::string& portName) const {
    const_cast<std::string&>(m_lastError) = "getBoolOutput not supported for PyTorch wrapper";
    return false;
}
void )WRAPGENCODE" << cls << R"WRAPGENCODE(::setStringInput(const std::string& portName, const std::string& value) {
    m_lastError = "setStringInput not supported for PyTorch wrapper";
}
std::string )WRAPGENCODE" << cls << R"WRAPGENCODE(::getStringOutput(const std::string& portName) const {
    const_cast<std::string&>(m_lastError) = "getStringOutput not supported for PyTorch wrapper";
    return "";
}
void )WRAPGENCODE" << cls << R"WRAPGENCODE(::setTensorInput(const std::string& portName, const std::vector<float>& tensorData) {
    // Tensor inputs are supported only through the structured step.
    m_lastError = "PyTorch wrapper uses structured step for tensor inputs; use step(Inputs, Outputs)";
}

// ---------- diagnostics ----------
std::string )WRAPGENCODE" << cls << R"WRAPGENCODE(::getBlockName() const {
    return m_blockName;
}
std::string )WRAPGENCODE" << cls << R"WRAPGENCODE(::getLastError() const {
    return m_lastError;
}
std::vector<std::string> )WRAPGENCODE" << cls << R"WRAPGENCODE(::getPortList() const {
    // PyTorch runtime doesn't expose named ports
    return {};
}

} // namespace )WRAPGENCODE" << nsCpp << R"WRAPGENCODE(
)WRAPGENCODE";
    } // end wrapper source

    GeneratedWrapperFiles files;
    files.headerPath = wrapperHeader.string();
    files.sourcePath = wrapperSource.string();
    files.runtimeHeaderPath = rtHeader.string();
    files.runtimeSourcePath = rtSource.string();
    return files;
}

GeneratedWrapperFiles WrapperGenerator::generate(const PackagingTaskRequestLite& req,
    const json& detectedMeta,
    const std::string& outDirGeneratedSrc) {

    (void)detectedMeta;

    fs::path base(outDirGeneratedSrc);
    fs::path incDir = base / "include";
    fs::path srcDir = base / "src";
    ensure_dir(incDir);
    ensure_dir(srcDir);

    const std::string cls = req.generationOptions.className;
    const std::string nsCpp = to_cpp_namespace(req.generationOptions.nameSpace);

    // For TF/PT: generate stub runtime (does not implement inference)
    const std::string& implType = req.implementationType;
    const bool isOnnx = (implType.empty() || implType == "ONNX");
    if (!isOnnx) {
        if (implType == "PyTorch") {
            return generateTorch(req, detectedMeta, incDir, srcDir, cls, nsCpp);
        }
        else {
            // 其他非 ONNX 框架（如 TensorFlow）暂用 stub
            return generateStub(req, implType, incDir, srcDir, cls, nsCpp);
        }
    }

    const std::string rtCls = "OnnxModelRuntime";

    fs::path rtHeader = incDir / (rtCls + ".h");
    fs::path rtSource = srcDir / (rtCls + ".cpp");

    fs::path wrapperHeader = incDir / (cls + ".h");
    fs::path wrapperSource = srcDir / (cls + ".cpp");

    std::vector<FieldInfo> inputFields = collectFields("input", req.portMapping, req.expectedBindings);
    std::vector<FieldInfo> outputFields = collectFields("output", req.portMapping, req.expectedBindings);

    // ----------------------------
    // 1) Write runtime header
    // ----------------------------
    {
        std::ofstream f(rtHeader.string(), std::ios::binary);
        f <<
            R"(#pragma once
#include "utils/json.hpp"
#include <onnxruntime_cxx_api.h>

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <memory>

class OnnxModelRuntime {
public:
    OnnxModelRuntime(const std::string& modelPath, const std::string& metadataJsonPath);
    ~OnnxModelRuntime();

    bool init();
    bool run(double time, double stepSize);
    void terminate();

    // inputs
    void setRealInput(const std::string& portName, double value);
    void setIntInput(const std::string& portName, int value);
    void setBoolInput(const std::string& portName, bool value);
    void setStringInput(const std::string& portName, const std::string& value);
    void setTensorInput(const std::string& portName, const std::vector<float>& tensorData);

    // scalar outputs
    double getRealOutput(const std::string& portName) const;
    int getIntOutput(const std::string& portName) const;
    bool getBoolOutput(const std::string& portName) const;
    std::string getStringOutput(const std::string& portName) const;

    // full tensor output normalized to float32
    std::vector<float> getTensorOutputFloat32(const std::string& portName) const;

    std::vector<std::string> getPortList() const;
    std::string lastError() const { return m_lastError; }

private:
    struct PortBinding {
        size_t ortIndex = 0;
        size_t flatOffset = 0;
        size_t elemCount = 1;
        std::vector<int64_t> shape;
        std::string dataType;
    };

    static std::vector<int64_t> jsonShapeToVector(const nlohmann::ordered_json& shapeJson);
    static size_t safeElemCountFromShape(const std::vector<int64_t>& shape);
    static bool isStringType(const std::string& t);
    static bool tryParseJsonStringArray(const std::string& s, std::vector<std::string>& out, std::string& err);

private:
    Ort::Env m_env;
    Ort::Session* m_session = nullptr;
    Ort::SessionOptions m_sessionOptions;

    nlohmann::ordered_json m_metadata;

    std::map<std::string, PortBinding> m_inputs;
    std::map<std::string, PortBinding> m_outputs;

    std::vector<std::variant<float, double, int, std::string>> m_inputValues;
    std::vector<std::variant<float, double, int, std::string>> m_outputValues;

    std::map<std::string, std::vector<std::string>> m_stringInputs;
    std::map<std::string, std::vector<std::string>> m_stringOutputs;

    // Dynamic 1D tensor inputs (shape [-1]) stored here
    std::map<std::string, std::vector<float>> m_tensorInputsF32;
    std::map<std::string, int64_t> m_dynamicLen;

    // Full numeric tensor outputs normalized to float32
    std::map<std::string, std::vector<float>> m_tensorOutputsF32;

    std::vector<char*> m_inputNodeNamesOwned;
    std::vector<char*> m_outputNodeNamesOwned;
    std::vector<const char*> m_inputNodeNames;
    std::vector<const char*> m_outputNodeNames;

    std::string m_modelPath;
    std::string m_lastError;
};
)";
    }

    // ----------------------------
// 2) Write runtime source (split into chunks to avoid MSVC C2026)
// ----------------------------
{
    std::ofstream f(rtSource.string(), std::ios::binary);

    // ===== Chunk 1/5: includes + JSON helpers + basic utils =====
    f << R"(#include "OnnxModelRuntime.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iostream>

using json = nlohmann::ordered_json;

static bool j_is_obj(const json& j) { return j.is_object(); }
static bool j_is_arr(const json& j) { return j.is_array(); }

static bool j_get_arr(const json& root, const char* key, const json*& out) {
    out = nullptr;
    if (!root.is_object()) return false;
    auto it = root.find(key);
    if (it == root.end() || !it->is_array()) return false;
    out = &(*it);
    return true;
}

static bool j_get_str(const json& obj, const char* key, std::string& out) {
    out.clear();
    if (!obj.is_object()) return false;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

static bool j_get_i64(const json& obj, const char* key, int64_t& out) {
    out = 0;
    if (!obj.is_object()) return false;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) return false;
    out = it->get<int64_t>();
    return true;
}

static std::string shape_to_string(const std::vector<int64_t>& s) {
    std::string out = "[";
    for (size_t i = 0; i < s.size(); ++i) {
        out += std::to_string(s[i]);
        if (i + 1 < s.size()) out += ",";
    }
    out += "]";
    return out;
}

static char* twin_strdup(const std::string& s) {
#ifdef _WIN32
    return _strdup(s.c_str());
#else
    return ::strdup(s.c_str());
#endif
}

std::vector<int64_t> OnnxModelRuntime::jsonShapeToVector(const nlohmann::ordered_json& shapeJson) {
    std::vector<int64_t> shape;
    if (shapeJson.is_array()) {
        for (auto& v : shapeJson) {
            if (v.is_number_integer()) shape.push_back(v.get<int64_t>());
        }
    }
    if (shape.empty()) shape = { 1 };
    return shape;
}

size_t OnnxModelRuntime::safeElemCountFromShape(const std::vector<int64_t>& shape) {
    // For fixed shapes only. If shape has -1, caller must handle dynamically.
    if (shape.empty()) return 1;
    size_t count = 1;
    for (auto d : shape) {
        if (d <= 0) return 0; // invalid for fixed path (includes -1)
        count *= static_cast<size_t>(d);
    }
    return count;
}

bool OnnxModelRuntime::isStringType(const std::string& t) {
    return t == "String" || t == "string" || t == "STRING";
}

bool OnnxModelRuntime::tryParseJsonStringArray(const std::string& s, std::vector<std::string>& out, std::string& err) {
    out.clear();
    err.clear();
    try {
        auto j = json::parse(s);
        if (!j.is_array()) {
            err = "JSON is not array";
            return false;
        }
        for (auto& it : j) {
            if (!it.is_string()) {
                err = "JSON array contains non-string element";
                return false;
            }
            out.push_back(it.get<std::string>());
        }
        return true;
    }
    catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}
)";

    // ===== Chunk 2/5: ctor parses metadata, supports 1D dynamic [-1] =====
    f << R"(
OnnxModelRuntime::OnnxModelRuntime(const std::string& modelPath, const std::string& metadataJsonPath)
    : m_env(ORT_LOGGING_LEVEL_WARNING, "OnnxModelRuntime"),
      m_modelPath(modelPath) {

    std::ifstream fmeta(metadataJsonPath, std::ios::binary);
    if (!fmeta.is_open()) {
        m_lastError = "Cannot open metadata json: " + metadataJsonPath;
        return;
    }

    try {
        fmeta >> m_metadata;
    }
    catch (const std::exception& e) {
        m_lastError = std::string("Failed to parse metadata json: ") + e.what();
        return;
    }

    const json* inputs = nullptr;
    const json* outputs = nullptr;
    if (!j_get_arr(m_metadata, "inputs", inputs) || !j_get_arr(m_metadata, "outputs", outputs)) {
        m_lastError = "Metadata json missing 'inputs'/'outputs' arrays";
        return;
    }

    size_t flatOffset = 0;

    for (size_t i = 0; i < inputs->size(); ++i) {
        const json& in = (*inputs)[i];
        if (!j_is_obj(in)) { m_lastError = "inputs[" + std::to_string(i) + "] is not object"; return; }

        std::string name, dataType;
        int64_t idx64 = -1;

        if (!j_get_str(in, "name", name) || name.empty()) { m_lastError = "inputs[" + std::to_string(i) + "] missing name"; return; }
        if (!j_get_i64(in, "index", idx64) || idx64 < 0) { m_lastError = "inputs[" + std::to_string(i) + "] missing/invalid index"; return; }
        if (!j_get_str(in, "dataType", dataType) || dataType.empty()) { m_lastError = "inputs[" + std::to_string(i) + "] missing dataType"; return; }

        auto itShape = in.find("shape");
        if (itShape == in.end() || !itShape->is_array()) { m_lastError = "inputs[" + std::to_string(i) + "] missing shape"; return; }
        std::vector<int64_t> shape = jsonShapeToVector(*itShape);

        PortBinding pb;
        pb.ortIndex = static_cast<size_t>(idx64);
        pb.flatOffset = flatOffset;
        pb.shape = shape;
        pb.dataType = dataType;

        // 判断是否为动态形状（任意维度为 -1）
        bool isDynamic = false;
        for (auto d : shape) {
            if (d == -1) { isDynamic = true; break; }
        }

        if (isStringType(dataType)) {
            pb.elemCount = isDynamic ? 0 : safeElemCountFromShape(shape);
            if (!isDynamic && pb.elemCount == 0) {
                m_lastError = "Invalid fixed shape for string input " + name + ": " + shape_to_string(shape);
                return;
            }
        } else {
            if (isDynamic) {
                pb.elemCount = 0;            // 标记为动态
            } else {
                pb.elemCount = safeElemCountFromShape(shape);
                if (pb.elemCount == 0) {
                    m_lastError = "Invalid fixed shape for input " + name + ": " + shape_to_string(shape);
                    return;
                }
                flatOffset += pb.elemCount;
            }
        }
        m_inputs[name] = pb;

        char* dup = twin_strdup(name);
        m_inputNodeNamesOwned.push_back(dup);
        m_inputNodeNames.push_back(dup);
    }

    for (size_t i = 0; i < outputs->size(); ++i) {
        const json& out = (*outputs)[i];
        if (!j_is_obj(out)) { m_lastError = "outputs[" + std::to_string(i) + "] is not object"; return; }

        std::string name, dataType;
        int64_t idx64 = -1;

        if (!j_get_str(out, "name", name) || name.empty()) { m_lastError = "outputs[" + std::to_string(i) + "] missing name"; return; }
        if (!j_get_i64(out, "index", idx64) || idx64 < 0) { m_lastError = "outputs[" + std::to_string(i) + "] missing/invalid index"; return; }
        if (!j_get_str(out, "dataType", dataType) || dataType.empty()) { m_lastError = "outputs[" + std::to_string(i) + "] missing dataType"; return; }

        auto itShape = out.find("shape");
        if (itShape == out.end() || !itShape->is_array()) { m_lastError = "outputs[" + std::to_string(i) + "] missing shape"; return; }
        std::vector<int64_t> shape = jsonShapeToVector(*itShape);

        PortBinding pb;
        pb.ortIndex = static_cast<size_t>(idx64);
        pb.flatOffset = 0;
        pb.shape = shape;
        pb.dataType = dataType;

        const bool isDyn1D = (shape.size() == 1 && shape[0] == -1);
        if (isStringType(dataType)) pb.elemCount = isDyn1D ? 0 : safeElemCountFromShape(shape);
        else pb.elemCount = isDyn1D ? 0 : safeElemCountFromShape(shape);

        m_outputs[name] = pb;

        char* dup = twin_strdup(name);
        m_outputNodeNamesOwned.push_back(dup);
        m_outputNodeNames.push_back(dup);
    }

    m_inputValues.assign(flatOffset, 0.0);
    m_outputValues.clear();
}

OnnxModelRuntime::~OnnxModelRuntime() {
    terminate();
    for (auto p : m_inputNodeNamesOwned) free(p);
    for (auto p : m_outputNodeNamesOwned) free(p);
    m_inputNodeNamesOwned.clear();
    m_outputNodeNamesOwned.clear();
}

bool OnnxModelRuntime::init() {
    if (!m_lastError.empty()) return false;
    try {
        m_sessionOptions.SetIntraOpNumThreads(1);
#ifdef _WIN32
        std::wstring wpath(m_modelPath.begin(), m_modelPath.end());
        m_session = new Ort::Session(m_env, wpath.c_str(), m_sessionOptions);
#else
        m_session = new Ort::Session(m_env, m_modelPath.c_str(), m_sessionOptions);
#endif
        return true;
    }
    catch (const Ort::Exception& e) {
        m_lastError = e.what();
        return false;
    }
}

void OnnxModelRuntime::terminate() {
    delete m_session;
    m_session = nullptr;
}

std::vector<std::string> OnnxModelRuntime::getPortList() const {
    std::vector<std::string> ports;
    for (const auto& p : m_inputs) ports.push_back("IN:" + p.first);
    for (const auto& p : m_outputs) ports.push_back("OUT:" + p.first);
    return ports;
}
)";

    // ===== Chunk 3/5: setters/getters; dynamic 1D inputs stored separately =====
    f << R"(
void OnnxModelRuntime::setRealInput(const std::string& portName, double value) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) return;
    const auto& bind = it->second;
    if (isStringType(bind.dataType)) return;
    if (!bind.shape.empty() && bind.shape.size() == 1 && bind.shape[0] == -1) {
        // dynamic 1D: store single element as length=1 tensor
        m_tensorInputsF32[portName] = { static_cast<float>(value) };
        m_dynamicLen[portName] = 1;
        return;
    }
    if (bind.flatOffset < m_inputValues.size()) m_inputValues[bind.flatOffset] = value;
}

void OnnxModelRuntime::setIntInput(const std::string& portName, int value) {
    setRealInput(portName, static_cast<double>(value));
}

void OnnxModelRuntime::setBoolInput(const std::string& portName, bool value) {
    setIntInput(portName, value ? 1 : 0);
}

void OnnxModelRuntime::setStringInput(const std::string& portName, const std::string& value) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) return;
    const auto& bind = it->second;
    if (!isStringType(bind.dataType)) return;

    std::vector<std::string> arr;
    std::string err;
    if (tryParseJsonStringArray(value, arr, err)) m_stringInputs[portName] = arr;
    else m_stringInputs[portName] = { value };

    // For dynamic 1D string tensor, record length for shape substitution
    if (!bind.shape.empty() && bind.shape.size() == 1 && bind.shape[0] == -1) {
        m_dynamicLen[portName] = static_cast<int64_t>(m_stringInputs[portName].size());
    }
}

void OnnxModelRuntime::setTensorInput(const std::string& portName, const std::vector<float>& tensorData) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) { m_lastError = "Tensor port not found: " + portName; return; }
    const auto& bind = it->second;
    if (isStringType(bind.dataType)) { m_lastError = "TensorInput cannot set String port: " + portName; return; }

    // 使用 elemCount == 0 判断动态
    const bool isDynamic = (bind.elemCount == 0);
    if (isDynamic) {
        if (tensorData.empty()) {
            m_lastError = "Dynamic tensor cannot be empty: " + portName;
            return;
        }
        m_tensorInputsF32[portName] = tensorData;
        return;
    }

    if (bind.flatOffset + bind.elemCount > m_inputValues.size() || tensorData.size() != bind.elemCount) {
        m_lastError = "Tensor data size mismatch: " + portName;
        return;
    }
    for (size_t i = 0; i < tensorData.size(); ++i) m_inputValues[bind.flatOffset + i] = tensorData[i];
}

double OnnxModelRuntime::getRealOutput(const std::string& portName) const {
    // Keep behavior: if tensor output exists, return first element; else 0.
    auto it = m_tensorOutputsF32.find(portName);
    if (it != m_tensorOutputsF32.end() && !it->second.empty()) return static_cast<double>(it->second[0]);
    return 0.0;
}
int OnnxModelRuntime::getIntOutput(const std::string& portName) const { return static_cast<int>(getRealOutput(portName)); }
bool OnnxModelRuntime::getBoolOutput(const std::string& portName) const { return getRealOutput(portName) > 0.5; }

std::string OnnxModelRuntime::getStringOutput(const std::string& portName) const {
    auto it = m_stringOutputs.find(portName);
    if (it != m_stringOutputs.end()) {
        json j = json::array();
        for (auto& s : it->second) j.push_back(s);
        return j.dump();
    }
    return "";
}

std::vector<float> OnnxModelRuntime::getTensorOutputFloat32(const std::string& portName) const {
    auto it = m_tensorOutputsF32.find(portName);
    if (it != m_tensorOutputsF32.end()) return it->second;
    return {};
}
)";

    // ===== Chunk 4/5: run(); substitute [-1] with actual N; enforce batch consistency =====
    f << R"custom(
bool OnnxModelRuntime::run(double time, double stepSize) {
    (void)time; (void)stepSize;
    if (!m_session) { m_lastError = "session not initialized"; return false; }
    if (!m_lastError.empty()) return false;

    // Enforce: all dynamic 1D inputs share the same batch size (industry norm)
    // We infer batch size from first dynamic input that has been set.
    int64_t batchN = -1;
    for (const auto& kv : m_dynamicLen) {
        const auto& port = kv.first;
        const int64_t n = kv.second;
        auto itIn = m_inputs.find(port);
        if (itIn == m_inputs.end()) continue;
        const auto& bind = itIn->second;
        const bool isDyn1D = (!bind.shape.empty() && bind.shape.size() == 1 && bind.shape[0] == -1);
        if (!isDyn1D) continue;
        if (n <= 0) { m_lastError = "Dynamic length invalid for port: " + port; return false; }
        if (batchN < 0) batchN = n;
        else if (batchN != n) {
            m_lastError = "Dynamic batch mismatch: expected N=" + std::to_string(batchN) +
                          " but port '" + port + "' has N=" + std::to_string(n);
            return false;
        }
    }

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(m_inputNodeNames.size());

        std::vector<std::vector<float>>   inFloatBuffers;
        std::vector<std::vector<double>>  inDoubleBuffers;
        std::vector<std::vector<int32_t>> inInt32Buffers;
        inFloatBuffers.reserve(m_inputNodeNames.size());
        inDoubleBuffers.reserve(m_inputNodeNames.size());
        inInt32Buffers.reserve(m_inputNodeNames.size());

        const json* inputs = nullptr;
        if (!j_get_arr(m_metadata, "inputs", inputs)) {
            m_lastError = "metadata inputs missing";
            return false;
        }

        for (size_t i = 0; i < inputs->size(); ++i) {
            const json& inMeta = (*inputs)[i];
            if (!j_is_obj(inMeta)) { m_lastError = "inputs[" + std::to_string(i) + "] is not object"; return false; }

            std::string portName, dataType;
            if (!j_get_str(inMeta, "name", portName) || !j_get_str(inMeta, "dataType", dataType)) {
                m_lastError = "invalid input meta at index " + std::to_string(i);
                return false;
            }

            auto itBind = m_inputs.find(portName);
            if (itBind == m_inputs.end()) { m_lastError = "missing input binding: " + portName; return false; }
            const PortBinding& bind = itBind->second;

            std::vector<int64_t> shape = bind.shape;
            // 判断是否为动态形状（任意维度包含 -1）
            bool isDynamic = false;
            for (auto d : shape) {
                if (d == -1) { isDynamic = true; break; }
            }

            if (isStringType(dataType)) {
                // 字符串输入处理（动态或固定）
                size_t elemCount = 0;
                if (isDynamic) {
                    auto itSI = m_stringInputs.find(portName);
                    if (itSI == m_stringInputs.end()) {
                        m_lastError = "Dynamic string input not set: " + portName;
                        return false;
                    }
                    elemCount = itSI->second.size();
                    // 推导动态维度
                    int64_t staticProd = 1;
                    for (auto d : shape) if (d > 0) staticProd *= d;
                    if (staticProd == 0 || elemCount % staticProd != 0) {
                        m_lastError = "Cannot infer dynamic shape for string input: " + portName;
                        return false;
                    }
                    int64_t dynVal = static_cast<int64_t>(elemCount) / staticProd;
                    for (auto& d : shape) if (d == -1) d = dynVal;
                } else {
                    elemCount = bind.elemCount;
                }

                Ort::Value strTensor = Ort::Value::CreateTensor(
                    allocator, shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING);

                std::vector<std::string> values;
                auto itSI = m_stringInputs.find(portName);
                if (itSI != m_stringInputs.end()) values = itSI->second;

                if (values.empty()) values.assign(elemCount, "");
                if (values.size() == 1 && elemCount > 1) values.assign(elemCount, values[0]);
                if (values.size() != elemCount) {
                    m_lastError = "String array element count mismatch: port=" + portName +
                                  " got=" + std::to_string(values.size()) + " expected=" + std::to_string(elemCount);
                    return false;
                }

                std::vector<const char*> cstrs(elemCount);
                for (size_t k = 0; k < elemCount; ++k) cstrs[k] = values[k].c_str();
                strTensor.FillStringTensor(cstrs.data(), cstrs.size());
                inputTensors.emplace_back(std::move(strTensor));
                continue;
            }

            // 数值类型动态输入
            if (isDynamic) {
                auto itBuf = m_tensorInputsF32.find(portName);
                if (itBuf == m_tensorInputsF32.end()) {
                    m_lastError = "Dynamic tensor input not set: " + portName;
                    return false;
                }
                const auto& data = itBuf->second;
                if (data.empty()) {
                    m_lastError = "Dynamic tensor input is empty: " + portName;
                    return false;
                }

                // 计算静态维度乘积
                int64_t staticProd = 1;
                for (auto d : shape) if (d > 0) staticProd *= d;
                if (staticProd == 0) {
                    // 所有维度都是动态的，直接使用数据大小作为形状（展平为一维）
                    shape = { static_cast<int64_t>(data.size()) };
                } else {
                    if (data.size() % staticProd != 0) {
                        m_lastError = "Dynamic tensor size mismatch for port " + portName +
                                      ": data.size=" + std::to_string(data.size()) +
                                      ", staticProd=" + std::to_string(staticProd);
                        return false;
                    }
                    int64_t dynVal = static_cast<int64_t>(data.size()) / staticProd;
                    for (auto& d : shape) if (d == -1) d = dynVal;
                }

                if (dataType == "Float32") {
                    inputTensors.emplace_back(Ort::Value::CreateTensor<float>(
                        mem, const_cast<float*>(data.data()), data.size(), shape.data(), shape.size()));
                } else {
                    m_lastError = "Dynamic input supports Float32 only. port=" + portName + " type=" + dataType;
                    return false;
                }
                continue;
            }

            // fixed-shape numeric paths (use m_inputValues flat buffer)
            size_t offset = bind.flatOffset;
            size_t elemCount = bind.elemCount;

            if (dataType == "Float32") {
                inFloatBuffers.emplace_back(elemCount);
                auto& buffer = inFloatBuffers.back();
                for (size_t j = 0; j < elemCount; ++j) {
                    buffer[j] = std::visit([](auto&& v) -> float {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) return 0.0f;
                        else return static_cast<float>(v);
                    }, m_inputValues[offset + j]);
                }
                inputTensors.emplace_back(Ort::Value::CreateTensor<float>(mem, buffer.data(), elemCount, shape.data(), shape.size()));
            }
            else if (dataType == "Float64" || dataType == "Double") {
                inDoubleBuffers.emplace_back(elemCount);
                auto& buffer = inDoubleBuffers.back();
                for (size_t j = 0; j < elemCount; ++j) {
                    buffer[j] = std::visit([](auto&& v) -> double {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) return 0.0;
                        else return static_cast<double>(v);
                    }, m_inputValues[offset + j]);
                }
                inputTensors.emplace_back(Ort::Value::CreateTensor<double>(mem, buffer.data(), elemCount, shape.data(), shape.size()));
            }
            else if (dataType == "Int32" || dataType == "Int") {
                inInt32Buffers.emplace_back(elemCount);
                auto& buffer = inInt32Buffers.back();
                for (size_t j = 0; j < elemCount; ++j) {
                    buffer[j] = std::visit([](auto&& v) -> int32_t {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::string>) return 0;
                        else return static_cast<int32_t>(v);
                    }, m_inputValues[offset + j]);
                }
                inputTensors.emplace_back(Ort::Value::CreateTensor<int32_t>(mem, buffer.data(), elemCount, shape.data(), shape.size()));
            }
            else {
                m_lastError = "Unsupported input data type: " + dataType;
                return false;
            }
        }

        auto outputTensors = m_session->Run(
            Ort::RunOptions{ nullptr },
            m_inputNodeNames.data(), inputTensors.data(), inputTensors.size(),
            m_outputNodeNames.data(), m_outputNodeNames.size()
        );

        m_stringOutputs.clear();
        m_outputValues.clear();
        m_tensorOutputsF32.clear();

        const json* outputs = nullptr;
        if (!j_get_arr(m_metadata, "outputs", outputs)) {
            m_lastError = "metadata outputs missing";
            return false;
        }

        if (outputTensors.size() != outputs->size()) {
            m_lastError = "ORT output tensor count mismatch: got=" + std::to_string(outputTensors.size()) +
                          " expected=" + std::to_string(outputs->size());
            return false;
        }

        for (size_t oi = 0; oi < outputs->size(); ++oi) {
            const json& outMeta = (*outputs)[oi];
            if (!j_is_obj(outMeta)) { m_lastError = "outputs[" + std::to_string(oi) + "] is not object"; return false; }

            std::string outPortName, outType;
            if (!j_get_str(outMeta, "name", outPortName) || !j_get_str(outMeta, "dataType", outType)) {
                m_lastError = "invalid output meta at index " + std::to_string(oi);
                return false;
            }

            auto outInfo = outputTensors[oi].GetTensorTypeAndShapeInfo();
            size_t outCount = outInfo.GetElementCount();
            if (outCount == 0) outCount = 1;

            if (isStringType(outType)) {
                std::vector<std::string> vec;
                vec.reserve(outCount);
                for (size_t k = 0; k < outCount; ++k) {
                    size_t len = outputTensors[oi].GetStringTensorElementLength(k);
                    std::string s; s.resize(len);
                    outputTensors[oi].GetStringTensorElement(len, k, &s[0]);
                    vec.push_back(std::move(s));
                }
                m_stringOutputs[outPortName] = vec;
                m_outputValues.push_back(vec.empty() ? std::string("") : vec[0]);
                continue;
            }

            if (outType == "Float32") {
                const float* out = outputTensors[oi].GetTensorData<float>();
                m_outputValues.push_back(out[0]);
                std::vector<float> f32vec(outCount);
                for (size_t k = 0; k < outCount; ++k) f32vec[k] = out[k];
                m_tensorOutputsF32[outPortName] = std::move(f32vec);
            }
            else if (outType == "Float64" || outType == "Double") {
                const double* out = outputTensors[oi].GetTensorData<double>();
                m_outputValues.push_back(static_cast<float>(out[0]));
                std::vector<float> f32vec(outCount);
                for (size_t k = 0; k < outCount; ++k) f32vec[k] = static_cast<float>(out[k]);
                m_tensorOutputsF32[outPortName] = std::move(f32vec);
            }
            else if (outType == "Int32" || outType == "Int") {
                const int32_t* out = outputTensors[oi].GetTensorData<int32_t>();
                m_outputValues.push_back(static_cast<int>(out[0]));
                std::vector<float> f32vec(outCount);
                for (size_t k = 0; k < outCount; ++k) f32vec[k] = static_cast<float>(out[k]);
                m_tensorOutputsF32[outPortName] = std::move(f32vec);
            }
            else {
                m_lastError = "Unsupported output data type: " + outType;
                return false;
            }
        }

        return true;
    }
    catch (const Ort::Exception& e) {
        m_lastError = e.what();
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}
)custom";

} // end runtime source generation block

    // ----------------------------
    // 3) Inputs / Outputs structs
    // ----------------------------
    std::string inputsStructBody;
    for (const auto& fi : inputFields) inputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (inputsStructBody.empty()) inputsStructBody = "    // no mapped input fields\n";

    std::string outputsStructBody;
    for (const auto& fi : outputFields) outputsStructBody += "    " + fi.cppType + " " + fi.fieldName + "{};\n";
    if (outputsStructBody.empty()) outputsStructBody = "    // no mapped output fields\n";

    // ----------------------------
    // 4) wrapper header (继承 IModelBlock)
    // ----------------------------
    {
        std::ofstream f(wrapperHeader.string(), std::ios::binary);
        f <<
            R"(#pragma once
#include <string>
#include <vector>
#include <memory>
#include "OnnxModelRuntime.h"
#include "IModelBlock.h"

namespace )" << nsCpp << R"( {

struct Inputs {
)" << inputsStructBody << R"(};

struct Outputs {
)" << outputsStructBody << R"(};

class )" << cls << R"( : public IModelBlock {
public:
    )" << cls << R"(() = default;
    ~)" << cls << R"(();

    // --- Original lifecycle (kept for internal convenience) ---
    bool initialize(const std::string& modelPath, const std::string& metaJsonPath);
    bool preprocess();
    bool infer(double time, double stepSize);
    bool extractOutputs();
    void release();

    // --- IModelBlock interface ---
    ModelStatus init() override;
    ModelStatus configure(const std::string& configData) override;
    ModelStatus step(double time, double stepSize) override;
    ModelStatus reset() override;
    ModelStatus terminate() override;

    void setRealInput(const std::string& portName, double value) override;
    double getRealOutput(const std::string& portName) const override;
    void setIntInput(const std::string& portName, int value) override;
    int getIntOutput(const std::string& portName) const override;
    void setBoolInput(const std::string& portName, bool value) override;
    bool getBoolOutput(const std::string& portName) const override;
    void setStringInput(const std::string& portName, const std::string& value) override;
    std::string getStringOutput(const std::string& portName) const override;
    void setTensorInput(const std::string& portName, const std::vector<float>& tensorData) override;

    std::string getBlockName() const override;
    std::string getLastError() const override;
    std::vector<std::string> getPortList() const override;

    // --- High‑level step with structured I/O (kept for easy testing) ---
    bool step(const Inputs& in, Outputs& out, double time, double stepSize);

    // backward compatible alias
    std::string lastError() const { return m_lastError; }

private:
    std::unique_ptr<OnnxModelRuntime> m_rt;
    std::string m_lastError;
    std::string m_blockName;
    std::string m_modelPath;   // stored for reset / reconfigure
};

} // namespace )" << nsCpp << R"(
)";
    }

    // ----------------------------
    // 5) step() body
    // ----------------------------
    std::string stepBody;
    stepBody += "    if (!m_rt) { m_lastError = \"runtime not initialized\"; return false; }\n";
    stepBody += "    // Apply inputs\n";
    for (const auto& fi : inputFields) {
        const std::string& fn = fi.fieldName;
        const std::string& tn = fi.tensorName;
        if (fi.accessorKind == "string") {
            stepBody += "    m_rt->setStringInput(\"" + tn + "\", in." + fn + ");\n";
        }
        else if (fi.accessorKind == "tensor") {
            stepBody += "    m_rt->setTensorInput(\"" + tn + "\", in." + fn + ");\n";
        }
        else if (fi.accessorKind == "real") {
            stepBody += "    m_rt->setRealInput(\"" + tn + "\", in." + fn + ");\n";
        }
        else if (fi.accessorKind == "int") {
            stepBody += "    m_rt->setIntInput(\"" + tn + "\", in." + fn + ");\n";
        }
        else if (fi.accessorKind == "bool") {
            stepBody += "    m_rt->setBoolInput(\"" + tn + "\", in." + fn + ");\n";
        }
        else if (fi.accessorKind == "tensor_element") {
            stepBody += "    m_rt->setTensorInput(\"" + tn + "\", std::vector<float>{static_cast<float>(in." + fn + ")});\n";
        }
    }
    stepBody += "    if (!m_rt->run(time, stepSize)) { m_lastError = m_rt->lastError(); return false; }\n";
    stepBody += "    // Extract outputs\n";
    for (const auto& fi : outputFields) {
        const std::string& fn = fi.fieldName;
        const std::string& tn = fi.tensorName;
        if (fi.accessorKind == "string") {
            stepBody += "    out." + fn + " = m_rt->getStringOutput(\"" + tn + "\");\n";
        }
        else if (fi.accessorKind == "tensor") {
            stepBody += "    out." + fn + " = m_rt->getTensorOutputFloat32(\"" + tn + "\");\n";
        }
        else if (fi.accessorKind == "real") {
            stepBody += "    out." + fn + " = m_rt->getRealOutput(\"" + tn + "\");\n";
        }
        else if (fi.accessorKind == "int") {
            stepBody += "    out." + fn + " = m_rt->getIntOutput(\"" + tn + "\");\n";
        }
        else if (fi.accessorKind == "bool") {
            stepBody += "    out." + fn + " = m_rt->getBoolOutput(\"" + tn + "\");\n";
        }
        else if (fi.accessorKind == "tensor_element") {
            stepBody += "    { auto _vec = m_rt->getTensorOutputFloat32(\"" + tn + "\"); int _idx = " + std::to_string(fi.tensorElementIndex) + "; out." + fn
                + " = (_idx >= 0 && static_cast<size_t>(_idx) < _vec.size()) ? static_cast<" + fi.cppType + ">(_vec[_idx]) : " + fi.cppType + "{}; }\n";
        }
    }
    stepBody += "    return true;\n";

    // ----------------------------
// 6) wrapper source (IModelBlock overrides)
// ----------------------------
    {
        std::ofstream f(wrapperSource.string(), std::ios::binary);

        f << "#include \"" << cls << ".h\"\n";
        f << "#include <filesystem>\n\n";
        f << "namespace " << nsCpp << " {\n\n";

        // Destructor – unchanged
        f << cls << "::~" << cls << "(){\n    release();\n}\n\n";

        // Initialize – now also saves modelPath for later use
        f << "bool " << cls << "::initialize(const std::string& modelPath, const std::string& metaJsonPath) {\n"
            "    m_rt = std::make_unique<OnnxModelRuntime>(modelPath, metaJsonPath);\n"
            "    if (!m_rt->init()) {\n"
            "        m_lastError = m_rt->lastError();\n"
            "        return false;\n"
            "    }\n"
            "    m_modelPath = modelPath;\n"
            "    return true;\n"
            "}\n\n";

        f << "bool " << cls << "::preprocess() { return true; }\n\n";

        f << "bool " << cls << "::infer(double time, double stepSize) {\n"
            "    if (!m_rt) { m_lastError = \"runtime not initialized\"; return false; }\n"
            "    if (!m_rt->run(time, stepSize)) { m_lastError = m_rt->lastError(); return false; }\n"
            "    return true;\n"
            "}\n\n";

        f << "bool " << cls << "::extractOutputs() { return true; }\n\n";

        f << "void " << cls << "::release() {\n"
            "    if (m_rt) { m_rt->terminate(); m_rt.reset(); }\n"
            "}\n\n";

        // ---------- IModelBlock overrides ----------

        // init()
        f << "ModelStatus " << cls << "::init() {\n"
            "    // Runtime already initialised by initialize() – nothing extra needed\n"
            "    return ModelStatus::OK;\n"
            "}\n\n";

        // configure(configData) -> calls initialize with resolved meta json
        f << "ModelStatus " << cls << "::configure(const std::string& configData) {\n"
            "    namespace fs = std::filesystem;\n"
            "    std::error_code ec;\n"
            "    fs::path candidates[] = {\n"
            "        fs::path(\"Packaging_Result\") / \"raw-extract\" / \"ModelParser_Meta.json\",\n"
            "        fs::path(\"raw-extract\") / \"ModelParser_Meta.json\"\n"
            "    };\n"
            "    for (const auto& candidate : candidates) {\n"
            "        if (fs::exists(candidate, ec)) {\n"
            "            if (initialize(configData, candidate.string()))\n"
            "                return ModelStatus::OK;\n"
            "            else\n"
            "                return ModelStatus::ERROR;\n"
            "        }\n"
            "    }\n"
            "    m_lastError = \"ModelParser_Meta.json not found\";\n"
            "    return ModelStatus::ERROR;\n"
            "}\n\n";

        // step(time, stepSize) – simple run without structured I/O
        f << "ModelStatus " << cls << "::step(double time, double stepSize) {\n"
            "    if (!m_rt) { m_lastError = \"runtime not initialized\"; return ModelStatus::ERROR; }\n"
            "    if (!m_rt->run(time, stepSize)) {\n"
            "        m_lastError = m_rt->lastError();\n"
            "        return ModelStatus::ERROR;\n"
            "    }\n"
            "    return ModelStatus::OK;\n"
            "}\n\n";

        // reset()
        f << "ModelStatus " << cls << "::reset() {\n"
            "    release();\n"
            "    if (m_modelPath.empty()) {\n"
            "        m_lastError = \"Cannot reset without a saved model path\";\n"
            "        return ModelStatus::ERROR;\n"
            "    }\n"
            "    return configure(m_modelPath);\n"
            "}\n\n";

        // terminate()
        f << "ModelStatus " << cls << "::terminate() {\n"
            "    release();\n"
            "    return ModelStatus::OK;\n"
            "}\n\n";

        // Structured step – kept for compatibility
        f << "bool " << cls << "::step(const Inputs& in, Outputs& out, double time, double stepSize) {\n"
            << stepBody
            << "}\n\n";

        // ---------- Scalar I/O overrides ----------
        f << "void " << cls << "::setRealInput(const std::string& portName, double value) {\n"
            "    if (m_rt) m_rt->setRealInput(portName, value);\n"
            "}\n\n";
        f << "double " << cls << "::getRealOutput(const std::string& portName) const {\n"
            "    return m_rt ? m_rt->getRealOutput(portName) : 0.0;\n"
            "}\n\n";

        f << "void " << cls << "::setIntInput(const std::string& portName, int value) {\n"
            "    if (m_rt) m_rt->setIntInput(portName, value);\n"
            "}\n\n";
        f << "int " << cls << "::getIntOutput(const std::string& portName) const {\n"
            "    return m_rt ? m_rt->getIntOutput(portName) : 0;\n"
            "}\n\n";

        f << "void " << cls << "::setBoolInput(const std::string& portName, bool value) {\n"
            "    if (m_rt) m_rt->setBoolInput(portName, value);\n"
            "}\n\n";
        f << "bool " << cls << "::getBoolOutput(const std::string& portName) const {\n"
            "    return m_rt ? m_rt->getBoolOutput(portName) : false;\n"
            "}\n\n";

        f << "void " << cls << "::setStringInput(const std::string& portName, const std::string& value) {\n"
            "    if (m_rt) m_rt->setStringInput(portName, value);\n"
            "}\n\n";
        f << "std::string " << cls << "::getStringOutput(const std::string& portName) const {\n"
            "    return m_rt ? m_rt->getStringOutput(portName) : \"\";\n"
            "}\n\n";

        f << "void " << cls << "::setTensorInput(const std::string& portName, const std::vector<float>& tensorData) {\n"
            "    if (m_rt) m_rt->setTensorInput(portName, tensorData);\n"
            "}\n\n";

        // ---------- Diagnostics ----------
        f << "std::string " << cls << "::getBlockName() const {\n"
            "    return m_blockName;\n"
            "}\n\n";
        f << "std::string " << cls << "::getLastError() const {\n"
            "    return m_lastError;\n"
            "}\n\n";
        f << "std::vector<std::string> " << cls << "::getPortList() const {\n"
            "    return m_rt ? m_rt->getPortList() : std::vector<std::string>();\n"
            "}\n\n";

        f << "} // namespace " << nsCpp << "\n";
    }

    GeneratedWrapperFiles files;
    files.headerPath = wrapperHeader.string();
    files.sourcePath = wrapperSource.string();
    files.runtimeHeaderPath = rtHeader.string();
    files.runtimeSourcePath = rtSource.string();
    return files;
}

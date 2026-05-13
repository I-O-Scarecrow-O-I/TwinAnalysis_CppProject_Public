#include"ModelParser.h"
#include"AI_Adapter.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "utils/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static std::vector<std::string> find_files_containing(const std::string& dir, const std::string& substr) {
    std::vector<std::string> result;
    if (!fs::is_directory(dir)) return result;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.find(substr) != std::string::npos) {
                result.push_back(filename);
            }
        }
    }
    return result;
}

static bool is_string_type(const std::string& t) {
    return t == "String" || t == "string" || t == "STRING";
}

static bool is_bool_type(const std::string& t) {
    return t == "Bool" || t == "bool" || t == "BOOL";
}

static bool is_int_type(const std::string& t) {
    return t == "Int32" || t == "Int" || t == "int" || t == "INT";
}

static bool is_float32_type(const std::string& t) {
    return t == "Float32" || t == "float" || t == "FLOAT";
}

static bool is_float64_type(const std::string& t) {
    return t == "Float64" || t == "Double" || t == "double" || t == "DOUBLE";
}

static size_t elem_count_from_shape_json(const json& shapeJson) {
    if (!shapeJson.is_array() || shapeJson.empty()) return 1;
    size_t n = 1;
    for (auto& d : shapeJson) {
        long long v = 1;
        if (d.is_number_integer()) v = d.get<long long>();
        if (v <= 0) v = 1; // -1 / 0 -> 1
        n *= static_cast<size_t>(v);
    }
    return n;
}

static void dump_ports(AI_Adapter& adapter) {
    auto ports = adapter.getPortList();
    std::cout << "Ports:\n";
    for (auto& p : ports) std::cout << "  " << p << "\n";
}

static void feed_input_by_type(AI_Adapter& adapter, const json& inMeta) {
    const std::string name = inMeta["name"].get<std::string>();
    const std::string type = inMeta["dataType"].get<std::string>();
    const size_t elemCount = inMeta.contains("shape") ? elem_count_from_shape_json(inMeta["shape"]) : 1;

    // 1) String：支持 [N]，输入 JSON array
    if (is_string_type(type)) {
        json arr = json::array();
        for (size_t i = 0; i < elemCount; ++i) {
            arr.push_back(std::string("s") + std::to_string(i));
        }
        adapter.setStringInput(name, arr.dump());
        std::cout << "[TEST] setStringInput " << name << " = " << arr.dump() << "\n";
        return;
    }

    // 2) Bool：用 setBoolInput（标量端口），shape>1 时仅测试第一个元素（符合 IModelBlock 标量端口语义）
    if (is_bool_type(type)) {
        adapter.setBoolInput(name, true);
        std::cout << "[TEST] setBoolInput " << name << " = true\n";
        return;
    }

    // 3) Int：用 setIntInput（标量端口）
    if (is_int_type(type)) {
        adapter.setIntInput(name, 7);
        std::cout << "[TEST] setIntInput " << name << " = 7\n";
        return;
    }

    // 4) Float64：用 setRealInput（标量端口）
    if (is_float64_type(type)) {
        adapter.setRealInput(name, 1.5);
        std::cout << "[TEST] setRealInput " << name << " = 1.5\n";
        return;
    }

    // 5) Float32 或其它数值：如果 elemCount>1，用 setTensorInput（float）喂入；否则用 setRealInput
    //    说明：你的 IModelBlock 只有 setTensorInput(vector<float>)，所以这里以 float 为主进行 tensor 测试。
    if (is_float32_type(type) || (!is_string_type(type) && !is_int_type(type) && !is_bool_type(type))) {
        if (elemCount > 1) {
            std::vector<float> vec(elemCount);
            for (size_t i = 0; i < elemCount; ++i) vec[i] = static_cast<float>(i + 1); // 1,2,3,...
            adapter.setTensorInput(name, vec);
            std::cout << "[TEST] setTensorInput " << name << " elemCount=" << elemCount << " fill=1..N\n";
        }
        else {
            adapter.setRealInput(name, 1.0);
            std::cout << "[TEST] setRealInput " << name << " = 1.0\n";
        }
        return;
    }
}

static void print_output_by_type(AI_Adapter& adapter, const json& outMeta) {
    const std::string name = outMeta["name"].get<std::string>();
    const std::string type = outMeta["dataType"].get<std::string>();

    if (is_string_type(type)) {
        std::cout << "[OUT] " << name << " (String) = " << adapter.getStringOutput(name) << "\n";
        return;
    }
    if (is_bool_type(type)) {
        std::cout << "[OUT] " << name << " (Bool) = " << (adapter.getBoolOutput(name) ? "true" : "false") << "\n";
        return;
    }
    if (is_int_type(type)) {
        std::cout << "[OUT] " << name << " (Int) = " << adapter.getIntOutput(name) << "\n";
        return;
    }
    // Real（Float32/Float64/Double 等）
    std::cout << "[OUT] " << name << " (Real) = " << adapter.getRealOutput(name) << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "用法: 输入 ./models 路径下的 <模型文件名>\n";
        return 1;
    }

    std::cout << "=== ONNX/TF/PyTorch 模型解析器 v0.2 ===\n";
    const std::string modelFileName = argv[1];

    // 1) 解析并生成 metajson
    std::string jsonPath = ModelParser::parseAndSaveMetadata(modelFileName);
    if (!jsonPath.empty()) {
        std::cout << "\n解析成功！JSON 文件路径: " << jsonPath << "\n";
    }

    // 2) 选择 metajson（不唯一则取最新）
    auto nameResluts = find_files_containing("./metajsons", modelFileName);
    if (nameResluts.empty()) {
        std::cout << "未找到对应模型的 metaJson 文件: " << modelFileName << "\n";
        return 0;
    }
    if (nameResluts.size() != 1) {
        std::cout << "Warning! 对应模型的 metaJson 文件不唯一: " << modelFileName << "\n";
        for (size_t i = 0; i < nameResluts.size(); i++) {
            std::cout << nameResluts[i] << "\n";
        }
    }

    std::sort(nameResluts.begin(), nameResluts.end());  // 按文件名排序
    std::string metajsonsPath = "metajsons/" + nameResluts.back(); // 取最新

    // 3) 模型路径
    std::string modelPath = "models/" + modelFileName;
    std::cout << modelPath << "\n" << metajsonsPath << "\n";

    // 4) 读取 metajson
    json meta;
    {
        std::ifstream mf(metajsonsPath);
        if (!mf.is_open()) {
            std::cerr << "无法打开 metajson: " << metajsonsPath << "\n";
            return 1;
        }
        mf >> meta;
    }
    if (!meta.contains("inputs") || !meta.contains("outputs")) {
        std::cerr << "metajson 缺少 inputs/outputs 字段\n";
        return 1;
    }

    // 5) 初始化 adapter
    AI_Adapter adapter(modelPath, metajsonsPath);
    if (adapter.init() != ModelStatus::OK) {
        std::cout << "初始化失败: " << adapter.getLastError() << "\n";
        return 1;
    }

    dump_ports(adapter);

    // 6) 按类型喂入每个 input
    for (auto& in : meta["inputs"]) {
        feed_input_by_type(adapter, in);
        // 如果 setTensorInput 内部报错，你的 adapter 会写 m_lastError，这里打印出来方便诊断
        const auto err = adapter.getLastError();
        if (!err.empty()) {
            // 不强制退出：有些模型可能忽略部分输入或者端口不匹配，你可根据需要改为 return 1
            // 这里先打印信息
            // 注意：如果 err 只是历史错误，可能会误报；更严谨是 adapter 提供 clearLastError
            std::cout << "[WARN] after feed input, lastError=" << err << "\n";
        }
    }

    // 7) step
    auto st = adapter.step(0.0, 0.1);
    if (st != ModelStatus::OK) {
        std::cout << "step failed: " << adapter.getLastError() << "\n";
        adapter.terminate();
        return 1;
    }

    // 8) 按类型打印每个 output
    for (auto& out : meta["outputs"]) {
        print_output_by_type(adapter, out);
    }

    adapter.terminate();
    return 0;
}
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>
#include <cstdlib>
#include <algorithm>

#include "utils/json.hpp"

// CMake 注入（用于 include 和 类型名）
#ifndef WRAPPER_HEADER
#define WRAPPER_HEADER "PyTorchAntiJammingWrapper.h"
#endif

#ifndef WRAPPER_TYPE
#define WRAPPER_TYPE twin::generated::PyTorchAntiJammingWrapper
#endif

#ifndef PACKAGING_OUT_DIR_DEFAULT
#define PACKAGING_OUT_DIR_DEFAULT ""
#endif

#include WRAPPER_HEADER

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static void print_usage(const char* exe) {
    std::cerr
        << "Usage:\n"
        << "  " << exe << " <packaging_out_dir> <model_path> [--input-shape D1 D2 ...] [--time-steps N]\n\n"
        << "Arguments:\n"
        << "  packaging_out_dir : directory that CONTAINS Packaging_Result\n"
        << "  model_path        : path to .pt file\n"
        << "  --input-shape     : force exact input shape (space-separated integers)\n"
        << "  --time-steps      : default length for dynamic dim -1 (default 10000)\n";
}

static bool read_text_file(const fs::path& p, std::string& out) {
    out.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool read_json_file(const fs::path& p, json& out, std::string& err) {
    err.clear();
    out = {};
    std::string text;
    if (!read_text_file(p, text)) {
        err = "Cannot open file: " + p.string();
        return false;
    }
    try {
        out = json::parse(text);
        return true;
    }
    catch (const std::exception& e) {
        err = std::string("JSON parse failed: ") + e.what();
        return false;
    }
}

static std::vector<float> generate_random_floats(size_t count) {
    std::vector<float> data(count);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& v : data) v = dist(rng);
    return data;
}

int main(int argc, char* argv[]) {
    std::cout << "=== PyTorch Wrapper Test (IModelBlock Interface Validation) ===\n";
    std::cout << "cwd(before): " << fs::current_path().string() << "\n";

    if (argc >= 2) {
        std::string a1 = argv[1];
        if (a1 == "-h" || a1 == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        std::cerr << "[ERROR] Missing required arguments.\n";
        print_usage(argv[0]);
        return 2;
    }

    const std::string packagingOutDir = argv[1];
    const std::string modelPathArg = argv[2];

    // 解析可选参数
    std::vector<int64_t> customShape;
    int64_t timeSteps = 10000;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--input-shape") {
            while (i + 1 < argc) {
                std::string val = argv[++i];
                if (val.rfind("--", 0) == 0) { --i; break; }
                customShape.push_back(std::strtoll(val.c_str(), nullptr, 10));
            }
        }
        else if (a == "--time-steps" && i + 1 < argc) {
            timeSteps = std::strtoll(argv[++i], nullptr, 10);
            if (timeSteps <= 0) {
                std::cerr << "[ERROR] --time-steps must be positive.\n";
                return 2;
            }
        }
    }

    const fs::path repoRoot = fs::path(__FILE__).parent_path().parent_path();
    const fs::path outDirAbs = repoRoot / packagingOutDir;
    const fs::path packagingResultDirAbs = outDirAbs / "Packaging_Result";
    const fs::path metaPath = packagingResultDirAbs / "Detected_Model_Meta.json";

    std::cout << "repoRoot             : " << repoRoot.string() << "\n";
    std::cout << "packagingOutDir      : " << packagingOutDir << "\n";
    std::cout << "outDirAbs            : " << outDirAbs.string() << "\n";
    std::cout << "Packaging_Result dir : " << packagingResultDirAbs.string() << "\n";
    std::cout << "metaPath             : " << metaPath.string() << "\n";
    std::cout << "model_path(arg)      : " << modelPathArg << "\n";

    if (!fs::exists(packagingResultDirAbs)) {
        std::cerr << "[ERROR] Packaging_Result directory not found.\n";
        return 3;
    }
    if (!fs::exists(metaPath)) {
        std::cerr << "[ERROR] Detected_Model_Meta.json not found.\n";
        return 4;
    }

    json meta;
    std::string err;
    if (!read_json_file(metaPath, meta, err)) {
        std::cerr << "[ERROR] Meta JSON invalid: " << err << "\n";
        return 5;
    }
    if (!meta.contains("inputs") || !meta["inputs"].is_array() || meta["inputs"].empty()) {
        std::cerr << "[ERROR] Meta JSON missing inputs.\n";
        return 6;
    }

    // ==================== 自动读取所有输入的名称和形状 ====================
    std::vector<std::string> inputNames;
    std::vector<std::vector<int64_t>> inputShapes;
    for (const auto& inp : meta["inputs"]) {
        inputNames.push_back(inp["name"].get<std::string>());
        std::vector<int64_t> shape;
        for (const auto& d : inp["shape"]) {
            shape.push_back(d.get<int64_t>());
        }
        // 处理动态维度：--input-shape 仅作用于第一个输入（若提供），其他输入用 timeSteps 替换 -1
        if (!customShape.empty() && inputShapes.empty()) {
            shape = customShape;
        }
        else {
            for (auto& dim : shape) {
                if (dim == -1) dim = timeSteps;
            }
        }
        inputShapes.push_back(shape);
    }

    // ==================== 为每个输入生成随机数据 ====================
    std::vector<std::vector<float>> inputData(inputNames.size());
    for (size_t i = 0; i < inputNames.size(); ++i) {
        size_t count = 1;
        for (auto d : inputShapes[i]) count *= static_cast<size_t>(d);
        inputData[i] = generate_random_floats(count);
    }

    std::cout << "Model inputs:\n";
    for (size_t i = 0; i < inputNames.size(); ++i) {
        std::cout << "  " << inputNames[i] << " : [";
        for (size_t j = 0; j < inputShapes[i].size(); ++j) {
            if (j > 0) std::cout << ", ";
            std::cout << inputShapes[i][j];
        }
        std::cout << "]  data size = " << inputData[i].size() << "\n";
    }

    // 切换工作目录
    {
        std::error_code ec;
        fs::current_path(outDirAbs, ec);
        if (ec) {
            std::cerr << "[ERROR] Failed to set current_path: " << outDirAbs.string() << "\n";
            return 7;
        }
    }
    std::cout << "cwd(after): " << fs::current_path().string() << "\n";

    // 解析模型路径
    fs::path modelPath = fs::path(modelPathArg);
    if (modelPath.is_relative()) modelPath = repoRoot / modelPath;
    modelPath = fs::weakly_canonical(modelPath);
    std::cout << "modelPath(resolved)  : " << modelPath.string() << "\n";
    if (!fs::exists(modelPath)) {
        std::cerr << "[ERROR] Model file not found.\n";
        return 8;
    }

    WRAPPER_TYPE wrapper;

    // ========== IModelBlock 接口测试 ==========
    // ---- 诊断测试（未初始化） ----
    std::cout << "\n[IModelBlock] Pre-init diagnostics:\n";
    std::cout << "  getBlockName(): " << wrapper.getBlockName() << "\n";
    std::cout << "  getLastError(): " << wrapper.getLastError() << "\n";
    auto portList = wrapper.getPortList();
    std::cout << "  getPortList(): (pre-init) size = " << portList.size() << "\n";

    // ---- init() ----
    std::cout << "\n[IModelBlock] Testing init()...\n";
    if (wrapper.init() != ModelStatus::OK) {
        std::cerr << "[ERROR] init() failed: " << wrapper.getLastError() << "\n";
        return 9;
    }
    std::cout << "  init() returned OK\n";

    // ---- configure() ----
    std::cout << "\n[IModelBlock] Testing configure()...\n";
    ModelStatus cfgStatus = wrapper.configure(modelPath.string());
    if (cfgStatus != ModelStatus::OK) {
        std::cerr << "[ERROR] configure() failed with status " << (int)cfgStatus
            << " : " << wrapper.getLastError() << "\n";
        return 10;
    }
    std::cout << "  configure() returned OK\n";

    // ---- 诊断测试（初始化后） ----
    portList = wrapper.getPortList();
    std::cout << "  getPortList() size = " << portList.size() << "\n";

    // ---- 标量 I/O 测试（预期输出错误信息，但不会崩溃） ----
    std::cout << "\n[IModelBlock] Testing scalar I/O methods...\n";
    wrapper.setRealInput("dummy_port", 3.14);
    std::cout << "  setRealInput called (error: " << wrapper.getLastError() << ")\n";
    double realOut = wrapper.getRealOutput("dummy_port");
    std::cout << "  getRealOutput: " << realOut << " (error: " << wrapper.getLastError() << ")\n";

    wrapper.setIntInput("dummy_port", 42);
    std::cout << "  setIntInput called (error: " << wrapper.getLastError() << ")\n";
    int intOut = wrapper.getIntOutput("dummy_port");
    std::cout << "  getIntOutput: " << intOut << " (error: " << wrapper.getLastError() << ")\n";

    wrapper.setBoolInput("dummy_port", true);
    std::cout << "  setBoolInput called (error: " << wrapper.getLastError() << ")\n";
    bool boolOut = wrapper.getBoolOutput("dummy_port");
    std::cout << "  getBoolOutput: " << std::boolalpha << boolOut << " (error: " << wrapper.getLastError() << ")\n";

    wrapper.setStringInput("dummy_port", "hello");
    std::cout << "  setStringInput called (error: " << wrapper.getLastError() << ")\n";
    std::string strOut = wrapper.getStringOutput("dummy_port");
    std::cout << "  getStringOutput: \"" << strOut << "\" (error: " << wrapper.getLastError() << ")\n";

    // ---- 张量输入测试（预期输出错误） ----
    std::cout << "\n[IModelBlock] Testing setTensorInput...\n";
    wrapper.setTensorInput(inputNames[0], inputData[0]);
    std::cout << "  setTensorInput called (error: " << wrapper.getLastError() << ")\n";

    // ---- step(double, double) 测试 ----
    std::cout << "\n[IModelBlock] Testing step(double, double)...\n";
    ModelStatus stepStatus = wrapper.step(0.0, 0.1);
    if (stepStatus != ModelStatus::OK) {
        std::cout << "  step(double, double) returned error (expected): " << wrapper.getLastError() << "\n";
    }
    else {
        std::cout << "  step(double, double) returned OK (unexpected)\n";
    }

    // ========== 原有结构化 step 推理测试 ==========
    std::cout << "\n[Structured Step] Running structured inference (Inputs/Outputs)...\n";
    twin::generated::Inputs in{};   // 使用 using namespace twin::generated
    twin::generated::Outputs out{};

    // ↑↑↑ 用户需手动修改以下字段名 ↑↑↑
    // 根据生成的包装器头文件中 Inputs 结构体的成员名称修改。
    // 当前抗干扰模型示例：字段名为 input
    if (inputNames.size() > 0) {
        in.input = inputData[0];    // 第一个输入字段名
    }
    if (inputNames.size() > 1) {
        // 第二个输入字段名，例如 in.field2 = inputData[1];
    }

    if (!wrapper.step(in, out, 0.0, 0.1)) {
        std::cerr << "[ERROR] step(Inputs, Outputs) failed: " << wrapper.lastError() << "\n";
        wrapper.terminate();
        return 11;
    }

    // ↑↑↑ 用户需手动修改以下输出字段名 ↑↑↑
    const auto& result = out.output;   // 输出字段名
    std::cout << "[OK] Structured inference succeeded.\n";
    std::cout << "Output size: " << result.size() << " elements\n";
    std::cout << "First 5 values: ";
    for (size_t i = 0; i < std::min(result.size(), size_t(5)); ++i)
        std::cout << result[i] << " ";
    std::cout << "\n";

    // ---- reset() 测试 ----
    std::cout << "\n[IModelBlock] Testing reset()...\n";
    if (wrapper.reset() != ModelStatus::OK) {
        std::cerr << "[ERROR] reset() failed: " << wrapper.getLastError() << "\n";
        return 12;
    }
    std::cout << "  reset() returned OK\n";

    // 重置后应能再次推理（快速验证）
    {
        twin::generated::Outputs out2{};
        if (!wrapper.step(in, out2, 0.0, 0.1)) {
            std::cerr << "[WARNING] step after reset failed: " << wrapper.lastError() << "\n";
        }
        else {
            std::cout << "  step after reset succeeded (output size " << out2.output.size() << ")\n";
        }
    }

    // ---- terminate() 测试 ----
    std::cout << "\n[IModelBlock] Testing terminate()...\n";
    if (wrapper.terminate() != ModelStatus::OK) {
        std::cerr << "[ERROR] terminate() failed.\n";
        return 13;
    }
    std::cout << "  terminate() returned OK\n";

    std::cout << "\n[OK] All IModelBlock interface tests passed. PyTorch wrapper test finished.\n";
    return 0;
}

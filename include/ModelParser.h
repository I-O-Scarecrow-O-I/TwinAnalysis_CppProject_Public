#pragma once
#include "utils/json.hpp"
#include <string>

class ModelParser {
public:
    // 主接口：传入模型路径，返回生成的 json 文件路径
    static std::string parseAndSaveMetadata(const std::string& modelPath);

private:
    // 内部解析 ONNX（原 main.cpp 中的核心逻辑）
    static nlohmann::ordered_json parseONNX(const std::string& modelPath);

    // 辅助函数
    static bool isONNX(const std::string& path);
    static bool isTensorFlow(const std::string& path);
    static bool isPyTorch(const std::string& path);
    static std::string getFileExtension(const std::string& path);
};
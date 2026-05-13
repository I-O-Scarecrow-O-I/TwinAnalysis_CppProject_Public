#pragma once
#include "utils/json.hpp"
#include <string>

class DetectedModelMetaBuilder {
public:
    // Build from ModelParser metajson (ONNX path).
    static nlohmann::ordered_json buildFromModelParserMeta(const nlohmann::ordered_json& modelParserMeta,
        const std::string& framework);

    // Build from Python meta-extraction script JSON output (TensorFlow / PyTorch path).
    // The pythonScriptJson is the parsed stdout of extract_tensorflow_meta.py or extract_pytorch_meta.py.
    static nlohmann::ordered_json buildFromPythonScriptMeta(const nlohmann::ordered_json& pythonScriptJson,
        const std::string& framework,
        const std::string& modelName);
};
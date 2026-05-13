#pragma once
#include "IModelBlock.h"
#include "utils/json.hpp"
#include <onnxruntime_cxx_api.h>

#include <vector>
#include <map>
#include <string>
#include <variant>

class AI_Adapter : public IModelBlock {
public:
    AI_Adapter(const std::string& modelPath, const std::string& metadataJsonPath,
        const std::string& blockName = "AI_Model");
    virtual ~AI_Adapter() override;

    // ==================== IModelBlock V2.0 接口全实现 ====================
    virtual ModelStatus init() override;
    virtual ModelStatus configure(const std::string& configData) override;
    virtual ModelStatus step(double time, double stepSize) override;
    virtual ModelStatus reset() override;
    virtual ModelStatus terminate() override;

    virtual void setRealInput(const std::string& portName, double value) override;
    virtual double getRealOutput(const std::string& portName) const override;

    virtual void setIntInput(const std::string& portName, int value) override;
    virtual int getIntOutput(const std::string& portName) const override;

    virtual void setBoolInput(const std::string& portName, bool value) override;
    virtual bool getBoolOutput(const std::string& portName) const override;

    // 约定：当 metadata 中该端口 shape=[N] 且 type=String 时，
    // setStringInput 接收 JSON array 字符串，例如 ["a","b","c"]
    // 若传入普通字符串，则按广播规则填充 N 个元素。
    virtual void setStringInput(const std::string& portName, const std::string& value) override;
    virtual std::string getStringOutput(const std::string& portName) const override;

    virtual std::string getBlockName() const override;
    virtual std::string getLastError() const override;
    virtual std::vector<std::string> getPortList() const override;

    virtual void setTensorInput(const std::string& portName, const std::vector<float>& tensorData) override;

private:
    struct PortBinding {
        size_t ortIndex = 0;            // ORT input/output index
        size_t flatOffset = 0;          // 扁平 buffer offset（数值用）
        size_t elemCount = 1;           // 元素个数（shape 展平）
        std::vector<int64_t> shape;     // 运行时 shape（-1 已替换为 1）
        std::string dataType;           // metadata["dataType"]
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

    // 数值扁平缓冲（用于 float/double/int 等）
    std::vector<std::variant<float, double, int, std::string>> m_inputValues;
    std::vector<std::variant<float, double, int, std::string>> m_outputValues;

    // string tensor：端口名 -> 多元素字符串
    std::map<std::string, std::vector<std::string>> m_stringInputs;
    std::map<std::string, std::vector<std::string>> m_stringOutputs;

    // ORT Run 需要稳定的 node name 指针
    std::vector<char*> m_inputNodeNamesOwned;
    std::vector<char*> m_outputNodeNamesOwned;
    std::vector<const char*> m_inputNodeNames;
    std::vector<const char*> m_outputNodeNames;

    std::string m_modelPath;
    std::string m_blockName;
    std::string m_lastError;
};
#include "AI_Adapter.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <algorithm>

using json = nlohmann::ordered_json;

static char* twin_strdup(const std::string& s) {
#ifdef _WIN32
    return _strdup(s.c_str());
#else
    return ::strdup(s.c_str());
#endif
}

std::vector<int64_t> AI_Adapter::jsonShapeToVector(const nlohmann::ordered_json& shapeJson) {
    std::vector<int64_t> shape;
    if (shapeJson.is_array()) {
        for (auto& v : shapeJson) shape.push_back(v.get<int64_t>());
    }
    if (shape.empty()) shape = { 1 };
    for (auto& d : shape) if (d == -1) d = 1;
    return shape;
}

size_t AI_Adapter::safeElemCountFromShape(const std::vector<int64_t>& shape) {
    if (shape.empty()) return 1;
    size_t count = 1;
    for (auto d : shape) {
        if (d <= 0) d = 1;
        count *= static_cast<size_t>(d);
    }
    return count;
}

bool AI_Adapter::isStringType(const std::string& t) {
    return t == "String" || t == "string" || t == "STRING";
}

bool AI_Adapter::tryParseJsonStringArray(const std::string& s, std::vector<std::string>& out, std::string& err) {
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

AI_Adapter::AI_Adapter(const std::string& modelPath, const std::string& metadataJsonPath,
    const std::string& blockName)
    : m_env(ORT_LOGGING_LEVEL_WARNING, "AI_Adapter"),
    m_modelPath(modelPath),
    m_blockName(blockName) {

    std::ifstream f(metadataJsonPath);
    if (!f.is_open()) {
        m_lastError = "�޷��� metadata.json";
        return;
    }
    f >> m_metadata;
    f.close();

    // 1) ��������˿ڰ� + node names
    size_t flatOffset = 0;
    const auto& inputs = m_metadata["inputs"];
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& in = inputs[i];
        const std::string name = in["name"].get<std::string>();
        const size_t ortIndex = in["index"].get<size_t>();
        const std::string dataType = in["dataType"].get<std::string>();
        std::vector<int64_t> shape = jsonShapeToVector(in["shape"]);
        size_t elemCount = safeElemCountFromShape(shape);

        // string tensor��elemCount ������ N����� 1=B��
        // ��ֵ tensor��elemCount ��չƽ����
        PortBinding pb;
        pb.ortIndex = ortIndex;
        pb.flatOffset = flatOffset;
        pb.elemCount = elemCount;
        pb.shape = shape;
        pb.dataType = dataType;

        m_inputs[name] = pb;

        // name ָ�뱣��
        char* dup = twin_strdup(name);
        m_inputNodeNamesOwned.push_back(dup);
        m_inputNodeNames.push_back(dup);

        // ��ֵ flatOffset ֻ����ֵ�˿ڼ���
        if (!isStringType(dataType)) {
            flatOffset += elemCount;
        }
    }

    // 2) ��������˿ڰ� + node names
    const auto& outputs = m_metadata["outputs"];
    for (size_t i = 0; i < outputs.size(); ++i) {
        const auto& out = outputs[i];
        const std::string name = out["name"].get<std::string>();
        const size_t ortIndex = out["index"].get<size_t>();
        const std::string dataType = out["dataType"].get<std::string>();
        std::vector<int64_t> shape = jsonShapeToVector(out["shape"]);
        size_t elemCount = safeElemCountFromShape(shape);

        PortBinding pb;
        pb.ortIndex = ortIndex;
        pb.flatOffset = 0; // output ���ǰ��˿���ȡ������ flatOffset������ȫ��
        pb.elemCount = elemCount;
        pb.shape = shape;
        pb.dataType = dataType;

        m_outputs[name] = pb;

        char* dup = twin_strdup(name);
        m_outputNodeNamesOwned.push_back(dup);
        m_outputNodeNames.push_back(dup);
    }

    // 3) ������ֵ���뻺�壨����ֵ�˿ڵ�չƽ���ȣ�
    m_inputValues.assign(flatOffset, 0.0); // Ĭ�� double
    m_outputValues.clear();
}

AI_Adapter::~AI_Adapter() {
    terminate();

    // �ͷ� strdup/_strdup ���ڴ�
    for (auto p : m_inputNodeNamesOwned) free(p);
    for (auto p : m_outputNodeNamesOwned) free(p);
    m_inputNodeNamesOwned.clear();
    m_outputNodeNamesOwned.clear();
}

ModelStatus AI_Adapter::init() {
    try {
        m_sessionOptions.SetIntraOpNumThreads(1);
#ifdef _WIN32
        std::wstring wpath(m_modelPath.begin(), m_modelPath.end());
        m_session = new Ort::Session(m_env, wpath.c_str(), m_sessionOptions);
#else
        m_session = new Ort::Session(m_env, m_modelPath.c_str(), m_sessionOptions);
#endif
        return ModelStatus::OK;
    }
    catch (const Ort::Exception& e) {
        m_lastError = e.what();
        return ModelStatus::FATAL;
    }
}

ModelStatus AI_Adapter::configure(const std::string& configData) {
    (void)configData;
    return ModelStatus::OK;
}

ModelStatus AI_Adapter::step(double time, double stepSize) {
    (void)time;
    (void)stepSize;

    if (!m_session) return ModelStatus::FATAL;

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<Ort::Value> inputTensors;
        inputTensors.reserve(m_inputNodeNames.size());

        // ������ֵ buffer����֤ Run() ʱ�ڴ�����Ч
        std::vector<std::vector<float>>   inFloatBuffers;
        std::vector<std::vector<double>>  inDoubleBuffers;
        std::vector<std::vector<int32_t>> inInt32Buffers;

        inFloatBuffers.reserve(m_inputNodeNames.size());
        inDoubleBuffers.reserve(m_inputNodeNames.size());
        inInt32Buffers.reserve(m_inputNodeNames.size());

        // �� input ���� tensor���� metadata["inputs"] ��˳��
        const auto& inputs = m_metadata["inputs"];
        for (size_t i = 0; i < inputs.size(); ++i) {
            const auto& inMeta = inputs[i];
            const std::string portName = inMeta["name"].get<std::string>();
            const std::string dataType = inMeta["dataType"].get<std::string>();

            auto itBind = m_inputs.find(portName);
            if (itBind == m_inputs.end()) {
                m_lastError = "�ڲ������Ҳ�������˿ڰ�: " + portName;
                return ModelStatus::ERROR;
            }
            const PortBinding& bind = itBind->second;

            std::vector<int64_t> shape = bind.shape;
            size_t elemCount = bind.elemCount;

            // -------- String ���루֧�� shape=[N]��--------
            if (isStringType(dataType)) {
                Ort::Value strTensor = Ort::Value::CreateTensor(
                    allocator,
                    shape.data(),
                    shape.size(),
                    ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING
                );

                // ��ȡ�˿��ַ����б�
                std::vector<std::string> values;
                auto itSI = m_stringInputs.find(portName);
                if (itSI != m_stringInputs.end()) values = itSI->second;

                // ��δ���ã�Ĭ��ȫ��
                if (values.empty()) values.assign(elemCount, "");

                // ��ֻ����1��Ԫ�أ����㲥���
                if (values.size() == 1 && elemCount > 1) {
                    values.assign(elemCount, values[0]);
                }

                if (values.size() != elemCount) {
                    m_lastError = "String����Ԫ��������ƥ��: port=" + portName +
                        " expected=" + std::to_string(elemCount) +
                        " got=" + std::to_string(values.size());
                    return ModelStatus::ERROR;
                }

                // ORT ��Ҫ const char* ���飻��������ʱ vector<const char*>��ָ�� values �� c_str()
                // ע�⣺values ����ʱ�������� FillStringTensor �´���ַ����� Ort::Value �ڲ�����ȫ��
                std::vector<const char*> cstrs(elemCount);
                for (size_t k = 0; k < elemCount; ++k) cstrs[k] = values[k].c_str();

                strTensor.FillStringTensor(cstrs.data(), cstrs.size());
                inputTensors.emplace_back(std::move(strTensor));
                continue;
            }

            // -------- ��ֵ���루�� m_inputValues �� flatOffset ȡ��--------
            size_t offset = bind.flatOffset;

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
                inputTensors.emplace_back(
                    Ort::Value::CreateTensor<float>(mem, buffer.data(), elemCount, shape.data(), shape.size())
                );
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
                inputTensors.emplace_back(
                    Ort::Value::CreateTensor<double>(mem, buffer.data(), elemCount, shape.data(), shape.size())
                );
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
                inputTensors.emplace_back(
                    Ort::Value::CreateTensor<int32_t>(mem, buffer.data(), elemCount, shape.data(), shape.size())
                );
            }
            else {
                m_lastError = "��֧�ֵ���������: " + dataType;
                return ModelStatus::ERROR;
            }
        }

        // ===== Run������� =====
        auto outputTensors = m_session->Run(
            Ort::RunOptions{ nullptr },
            m_inputNodeNames.data(), inputTensors.data(), inputTensors.size(),
            m_outputNodeNames.data(), m_outputNodeNames.size()
        );

        if (outputTensors.size() != m_outputNodeNames.size()) {
            m_lastError = "ģ�����������һ��";
            return ModelStatus::ERROR;
        }

        // ��վ����
        m_stringOutputs.clear();
        m_outputValues.clear();

        // �� metadata["outputs"] ˳�����ÿ�����
        const auto& outputs = m_metadata["outputs"];
        for (size_t oi = 0; oi < outputs.size(); ++oi) {
            const auto& outMeta = outputs[oi];
            const std::string outPortName = outMeta["name"].get<std::string>();
            const std::string outType = outMeta["dataType"].get<std::string>();

            if (!outputTensors[oi].IsTensor()) {
                m_lastError = "�������Tensor: " + outPortName;
                return ModelStatus::ERROR;
            }

            auto outInfo = outputTensors[oi].GetTensorTypeAndShapeInfo();
            size_t outCount = outInfo.GetElementCount();
            if (outCount == 0) outCount = 1;

            // ---- String �����֧�� [N]��----
            if (isStringType(outType)) {
                std::vector<std::string> vec;
                vec.reserve(outCount);

                for (size_t k = 0; k < outCount; ++k) {
                    size_t len = outputTensors[oi].GetStringTensorElementLength(k);
                    std::string s;
                    s.resize(len);
                    outputTensors[oi].GetStringTensorElement(len, k, &s[0]);
                    vec.push_back(std::move(s));
                }
                m_stringOutputs[outPortName] = vec;

                // ͬʱ�ѵ�һ��Ԫ�طŵ� m_outputValues������ getStringOutput fallback��
                if (!vec.empty()) {
                    m_outputValues.push_back(vec[0]);
                }
                else {
                    m_outputValues.push_back(std::string(""));
                }
                continue;
            }

            // ---- ��ֵ�����ֻ�ѵ�һ��Ԫ��д�� m_outputValues��������ԭ getRealOutput ���----
            if (outType == "Float32") {
                const float* out = outputTensors[oi].GetTensorData<float>();
                m_outputValues.push_back(out[0]);
            }
            else if (outType == "Float64" || outType == "Double") {
                const double* out = outputTensors[oi].GetTensorData<double>();
                m_outputValues.push_back(out[0]);
            }
            else if (outType == "Int32" || outType == "Int") {
                const int32_t* out = outputTensors[oi].GetTensorData<int32_t>();
                m_outputValues.push_back(static_cast<int>(out[0]));
            }
            else {
                m_lastError = "��֧�ֵ��������: " + outType;
                return ModelStatus::ERROR;
            }
        }

        return ModelStatus::OK;
    }
    catch (const Ort::Exception& e) {
        m_lastError = e.what();
        return ModelStatus::ERROR;
    }
    catch (const std::exception& e) {
        m_lastError = e.what();
        return ModelStatus::ERROR;
    }
}

ModelStatus AI_Adapter::reset() {
    std::fill(m_inputValues.begin(), m_inputValues.end(), 0.0);
    m_stringInputs.clear();
    return ModelStatus::OK;
}

ModelStatus AI_Adapter::terminate() {
    delete m_session;
    m_session = nullptr;
    return ModelStatus::OK;
}

std::string AI_Adapter::getLastError() const { return m_lastError; }
std::string AI_Adapter::getBlockName() const { return m_blockName; }

std::vector<std::string> AI_Adapter::getPortList() const {
    std::vector<std::string> ports;
    for (const auto& p : m_inputs) ports.push_back("IN:" + p.first);
    for (const auto& p : m_outputs) ports.push_back("OUT:" + p.first);
    return ports;
}

void AI_Adapter::setRealInput(const std::string& portName, double value) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) return;
    const auto& bind = it->second;
    if (isStringType(bind.dataType)) return;
    if (bind.flatOffset < m_inputValues.size()) m_inputValues[bind.flatOffset] = value;
}

double AI_Adapter::getRealOutput(const std::string& portName) const {
    // ���ﰴ��outputs ˳�򡱴��� m_outputValues �Ĳ��ԣ�������˿��� metadata �е� index ˳��ӳ����ɿ�
    // �򻯣��� m_metadata["outputs"] ˳����λ��
    const auto& outputs = m_metadata["outputs"];
    for (size_t oi = 0; oi < outputs.size(); ++oi) {
        if (outputs[oi]["name"].get<std::string>() == portName) {
            if (oi < m_outputValues.size()) {
                const auto& val = m_outputValues[oi];
                return std::visit([](auto&& v) -> double {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>) return 0.0;
                    else return static_cast<double>(v);
                    }, val);
            }
        }
    }
    return 0.0;
}

void AI_Adapter::setIntInput(const std::string& portName, int value) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) return;
    const auto& bind = it->second;
    if (isStringType(bind.dataType)) return;
    if (bind.flatOffset < m_inputValues.size()) m_inputValues[bind.flatOffset] = value;
}

int AI_Adapter::getIntOutput(const std::string& portName) const {
    return static_cast<int>(getRealOutput(portName));
}

void AI_Adapter::setBoolInput(const std::string& portName, bool value) {
    setIntInput(portName, value ? 1 : 0);
}

bool AI_Adapter::getBoolOutput(const std::string& portName) const {
    return getRealOutput(portName) > 0.5;
}

void AI_Adapter::setStringInput(const std::string& portName, const std::string& value) {
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) return;
    const auto& bind = it->second;
    if (!isStringType(bind.dataType)) return;

    // Լ����֧������д��
    // 1) JSON array��["a","b","c"] -> [N]
    // 2) ��ͨ�ַ�����"hello" -> �㲥�� [N]
    std::vector<std::string> arr;
    std::string err;
    if (tryParseJsonStringArray(value, arr, err)) {
        m_stringInputs[portName] = arr;
    }
    else {
        m_stringInputs[portName] = { value };
    }
}

std::string AI_Adapter::getStringOutput(const std::string& portName) const {
    auto it = m_stringOutputs.find(portName);
    if (it != m_stringOutputs.end()) {
        // ��ҵ�������������� JSON array �ַ��������ⶪ��Ϣ
        json j = json::array();
        for (auto& s : it->second) j.push_back(s);
        return j.dump();
    }

    // fallback����� m_outputValues ��Ӧ���� string��������
    const auto& outputs = m_metadata["outputs"];
    for (size_t oi = 0; oi < outputs.size(); ++oi) {
        if (outputs[oi]["name"].get<std::string>() == portName) {
            if (oi < m_outputValues.size()) {
                const auto& v = m_outputValues[oi];
                if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
            }
        }
    }
    return "";
}

void AI_Adapter::setTensorInput(const std::string& portName, const std::vector<float>& tensorData) {
    // ����ӿ�Ŀǰ���ǡ���ƽ��ֵд�롱�ļ򻯰�
    auto it = m_inputs.find(portName);
    if (it == m_inputs.end()) {
        m_lastError = "Tensor�˿ڲ�����: " + portName;
        return;
    }
    const auto& bind = it->second;
    if (isStringType(bind.dataType)) {
        m_lastError = "TensorInput��������String�˿�: " + portName;
        return;
    }
    if (bind.flatOffset + bind.elemCount > m_inputValues.size() || tensorData.size() != bind.elemCount) {
        m_lastError = "Tensor�����С��ƥ��: " + portName;
        return;
    }
    for (size_t i = 0; i < tensorData.size(); ++i) {
        m_inputValues[bind.flatOffset + i] = tensorData[i];
    }
}

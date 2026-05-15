#include "ModelParser.h"
#include <onnxruntime_cxx_api.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <map>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <sys/stat.h>
#include <errno.h>    
#ifdef _WIN32
#  include <direct.h>
#endif

#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include "utils/json.hpp"

using json = nlohmann::ordered_json;

// 创建目录的函数
bool createDirectory(const std::string& path) {
    // Windows下创建目录
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    // Linux/macOS
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

// 获取当前时间戳
std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// 获取形状描述
std::vector<std::string> getShapeDescription(const std::vector<int64_t>& shape) {
    static const std::vector<std::string> commonDescriptions = {
        "BatchSize", "Channels", "Height", "Width",
        "Depth", "SequenceLength", "FeatureDim", "EmbeddingDim"
    };
    std::vector<std::string> descriptions;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i < commonDescriptions.size()) {
            descriptions.push_back(commonDescriptions[i]);
        }
        else {
            descriptions.push_back("Dim" + std::to_string(i));
        }
    }
    return descriptions;
}

// ==================== 数据类型映射 ====================
const std::map<ONNXTensorElementDataType, std::pair<std::string, int>> TYPE_MAP = {
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED, {"Undefined", 0}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, {"Float32", 4}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, {"UInt8", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8, {"Int8", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16, {"UInt16", 2}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16, {"Int16", 2}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32, {"Int32", 4}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, {"Int64", 8}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING, {"String", 0}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, {"Bool", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16, {"Float16", 2}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, {"Float64", 8}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32, {"UInt32", 4}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64, {"UInt64", 8}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64, {"Complex64", 8}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128, {"Complex128", 16}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16, {"BFloat16", 2}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FN, {"Float8E4M3FN", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FNUZ, {"Float8E4M3FNUZ", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2, {"Float8E5M2", 1}},
    {ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2FNUZ, {"Float8E5M2FNUZ", 1}},
};

std::string ModelParser::parseAndSaveMetadata(const std::string& modelPath) {
    std::string ext = getFileExtension(modelPath);

    if (isONNX(modelPath)) {
        std::cout << "检测到 ONNX 模型，开始解析...\n";

        try {
            auto result = parseONNX(modelPath);
            if (result.is_null()) {
                std::cerr << "parseONNX 解析失败" << std::endl;
                return "";
            }

            // 8. 确保metajsons文件夹存在
            std::string outputDir = "metajsons";

            // 尝试创建目录
            if (!createDirectory(outputDir)) {
                std::cerr << "警告: 无法创建输出目录 '" << outputDir << "'" << std::endl;
                std::cerr << "将保存到当前目录" << std::endl;
                outputDir = "";  // 使用当前目录
            }
            else {
                outputDir += "/";  // 添加路径分隔符
            }

            // 9. 生成输出文件名
            std::string timestamp = getCurrentTimestamp();
            std::replace(timestamp.begin(), timestamp.end(), ':', '-');
            std::replace(timestamp.begin(), timestamp.end(), ' ', '_');

            std::string outputFilename = outputDir + "metadata_" + timestamp + "_" + modelPath.substr(modelPath.find_last_of("/\\") + 1) + ".json";

            // 10. 写入文件
            std::ofstream jsonFile(outputFilename);
            if (!jsonFile.is_open()) {
                throw std::runtime_error("无法创建输出文件: " + outputFilename);
            }

            jsonFile << result.dump(2);
            jsonFile.close();
            std::cout << "ONNX 解析完成！文件已保存: " << outputFilename << std::endl;
            return outputFilename;
        }
        catch (const std::exception& e) {
            std::cerr << "解析失败: " << e.what() << std::endl;
            return "";
        }
    }
    else if (isTensorFlow(modelPath)) {
        std::cerr << "检测到 TensorFlow 模型 (.pb / SavedModel)。\n"
            << "请先使用 tf2onnx 导出为 ONNX：\n"
            << "python -m tf2onnx.convert --saved-model your_model --output model.onnx\n";
        return "";
    }
    else if (isPyTorch(modelPath)) {
        std::cerr << "检测到 PyTorch 模型 (.pth)。\n"
            << "请先使用 torch.onnx.export 导出为 ONNX。\n";
        return "";
    }
    else {
        std::cerr << "不支持的文件格式: " << ext << std::endl;
        return "";
    }
}

// ==================== 原 ONNX 解析逻辑 ====================
json ModelParser::parseONNX(const std::string& inputModelPath) {
    std::string modelPath = inputModelPath;
    try {
        // 判断参数是否包含路径分隔符（不是纯文件名）
        bool isRelativePath = (modelPath.find('/') == std::string::npos &&
            modelPath.find('\\') == std::string::npos &&
            modelPath.find('.') != std::string::npos);

        // 如果是纯文件名，则添加models目录前缀
        if (isRelativePath) {
            modelPath = "./models/" + modelPath;
        }

        // 检查文件是否存在
        std::ifstream testFile(modelPath);
        if (!testFile.good()) {
            std::cerr << "错误: 模型文件不存在或无法访问: " << modelPath << std::endl;
            if (isRelativePath) {
                std::cerr << "请确保模型文件位于 models/ 目录中" << std::endl;
            }
            return json::object(); // 返回空 json
        }
        testFile.close();

        std::cout << "正在解析模型: " << modelPath << std::endl;

        // 初始化ONNX Runtime环境
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ModelParser");

        // SessionOptions 配置
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);

        // 创建 Session（加载模型）
#ifdef _WIN32
        std::wstring wmodel_path(modelPath.begin(), modelPath.end());
        Ort::Session session(env, wmodel_path.c_str(), session_options);
#else
        Ort::Session session(env, modelPath.c_str(), session_options);
#endif

        std::cout << "模型加载成功" << std::endl;

        // 分配器
        Ort::AllocatorWithDefaultOptions allocator;

        // 构建JSON结构 - 按照要求的顺序
        json result;

        // 1. metaData部分 - 必须在最前面
        Ort::ModelMetadata model_metadata = session.GetModelMetadata();

        json metaData;
        // 获取模型名称，如果为空则使用文件名
        auto model_name_ptr = model_metadata.GetGraphNameAllocated(allocator);
        std::string modelName = model_name_ptr.get();
        if (modelName.empty() || modelName == "main" || modelName == "unknown") {
            // 从文件路径中提取文件名作为模型名
            std::filesystem::path modelPathObj(modelPath);
            modelName = modelPathObj.stem().string();  // 获取不带扩展名的文件名
        }
        metaData["modelName"] = modelName;

        // 获取生产者名称，如果为空则推断
        auto producer_name_ptr = model_metadata.GetProducerNameAllocated(allocator);
        std::string producerName = producer_name_ptr.get();
        if (producerName.empty()) {
            // 根据常见的生产者名称进行推断
            // 或者使用domain信息来推断
            auto domain_ptr = model_metadata.GetDomainAllocated(allocator);
            std::string domain = domain_ptr.get();

            if (domain.find("pytorch") != std::string::npos) {
                producerName = "PyTorch";
            }
            else if (domain.find("tensorflow") != std::string::npos) {
                producerName = "TensorFlow";
            }
            else if (domain.find("onnx") != std::string::npos) {
                producerName = "ONNX";
            }
            else {
                producerName = "Unknown";
            }
        }
        metaData["producerName"] = producerName;

        // 获取domain
        auto domain_ptr = model_metadata.GetDomainAllocated(allocator);
        metaData["domain"] = domain_ptr.get();

        // 处理version（有时会是极大值）
        int64_t version = model_metadata.GetVersion();
        if (version == 9223372036854775807LL || version < 0) {
            version = 1;  // 设置为合理的默认值
        }
        metaData["version"] = version;

        // 获取描述
        auto description_ptr = model_metadata.GetDescriptionAllocated(allocator);
        metaData["description"] = description_ptr.get();

        result["metaData"] = metaData;

        std::cout << "元数据提取完成" << std::endl;

        // 2. inputs部分 - 在metaData之后
        size_t num_inputs = session.GetInputCount();
        json inputs = json::array();

        for (size_t i = 0; i < num_inputs; ++i) {
            json input_info;

            // 按顺序添加字段
            auto input_name_ptr = session.GetInputNameAllocated(i, allocator);
            input_info["name"] = input_name_ptr.get();
            input_info["index"] = i;
            input_info["tensorType"] = "Tensor";

            // 获取类型和形状信息
            auto type_info = session.GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            ONNXTensorElementDataType data_type = tensor_info.GetElementType();

            auto it = TYPE_MAP.find(data_type);
            if (it != TYPE_MAP.end()) {
                input_info["dataType"] = it->second.first;
                input_info["elementSize"] = it->second.second;
            }
            else {
                input_info["dataType"] = "Unknown";
                input_info["elementSize"] = 0;
            }

            // 形状：支持动态-1
            std::vector<int64_t> shape = tensor_info.GetShape();
            input_info["shape"] = shape;

            // 形状描述
            input_info["shapeDescription"] = getShapeDescription(shape);

            inputs.push_back(input_info);
        }
        result["inputs"] = inputs;

        // 3. outputs部分 - 在inputs之后
        size_t num_outputs = session.GetOutputCount();
        json outputs = json::array();

        for (size_t i = 0; i < num_outputs; ++i) {
            json output_info;

            // 按顺序添加字段
            auto output_name_ptr = session.GetOutputNameAllocated(i, allocator);
            output_info["name"] = output_name_ptr.get();
            output_info["index"] = i;
            output_info["tensorType"] = "Tensor";

            auto type_info = session.GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            ONNXTensorElementDataType data_type = tensor_info.GetElementType();

            auto it = TYPE_MAP.find(data_type);
            if (it != TYPE_MAP.end()) {
                output_info["dataType"] = it->second.first;
                output_info["elementSize"] = it->second.second;
            }
            else {
                output_info["dataType"] = "Unknown";
                output_info["elementSize"] = 0;
            }

            std::vector<int64_t> shape = tensor_info.GetShape();
            output_info["shape"] = shape;

            output_info["shapeDescription"] = getShapeDescription(shape);

            outputs.push_back(output_info);
        }
        result["outputs"] = outputs;

        // 4. parameters部分 - 在outputs之后
        json parameters;
        parameters["requiresCUDA"] = false;
        result["parameters"] = parameters;

        std::cout << " 输入输出信息提取完成" << std::endl;

        // 5. 统计信息
        json statistics;
        statistics["totalInputs"] = num_inputs;
        statistics["totalOutputs"] = num_outputs;

        // 统计动态维度
        int dynamicInputs = 0;
        int dynamicOutputs = 0;
        try {
            for (const auto& input : inputs) {
                if (input.contains("shape") && input["shape"].is_array()) {
                    for (const auto& dim : input["shape"]) {
                        if (dim.is_number() && dim == -1) {
                            dynamicInputs++;
                            break;
                        }
                    }
                }
                else {
                    std::cerr << "警告: input shape 类型错误，不是 array，跳过动态检查" << std::endl;
                }
            }
            for (const auto& output : outputs) {
                if (output.contains("shape") && output["shape"].is_array()) {
                    for (const auto& dim : output["shape"]) {
                        if (dim.is_number() && dim == -1) {
                            dynamicOutputs++;
                            break;
                        }
                    }
                }
                else {
                    std::cerr << "警告: output shape 类型错误，不是 array，跳过动态检查" << std::endl;
                }
            }
        }
        catch (const json::exception& e) {
            std::cerr << "JSON 类型错误: " << e.what() << "，跳过动态统计" << std::endl;
            dynamicInputs = 0;
            dynamicOutputs = 0;
        }
     
        statistics["dynamicInputs"] = dynamicInputs;
        statistics["dynamicOutputs"] = dynamicOutputs;
        result["statistics"] = statistics;

        // 6. 模型文件信息
        json modelFileInfo;
        modelFileInfo["path"] = modelPath;

        //  获取文件大小
        std::ifstream file(modelPath, std::ios::binary | std::ios::ate);
        std::streamsize fileSize = file.tellg();
        file.close();

        modelFileInfo["sizeBytes"] = fileSize;
        modelFileInfo["sizeMB"] = fileSize / (1024.0 * 1024.0);
        result["modelFileInfo"] = modelFileInfo;

        // 7. 解析器信息（放在最后）
        json parserInfo;
        parserInfo["version"] = "0.1.0";
        parserInfo["generatedAt"] = getCurrentTimestamp();
        parserInfo["tool"] = "ONNX Model Parser";
        result["parserInfo"] = parserInfo;

        return result;
    }
    catch (const Ort::Exception& e) {
        std::cerr << "\n ONNX Runtime 异常: " << e.what() << std::endl;
        return json::object();
    }
    catch (const std::exception& e) {
        std::cerr << "\n 标准异常: " << e.what() << std::endl;
        return json::object();
    }
}

bool ModelParser::isONNX(const std::string& path) { return getFileExtension(path) == ".onnx"; }
bool ModelParser::isTensorFlow(const std::string& path) {
    std::string ext = getFileExtension(path);
    return ext == ".pb" || ext == ".savedmodel" || path.find("saved_model") != std::string::npos;
}
bool ModelParser::isPyTorch(const std::string& path) {
    std::string ext = getFileExtension(path);
    return ext == ".pth" || ext == ".pt";
}

std::string ModelParser::getFileExtension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    return (pos != std::string::npos) ? path.substr(pos) : "";
}

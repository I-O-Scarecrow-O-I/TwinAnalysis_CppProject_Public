#pragma once
#include <string>
#include <vector>
#include <optional>
#include <map>

// Port mapping: field name -> ONNX tensor name
using PortFieldMap = std::map<std::string, std::string>;
// Per-port field mapping: port name -> PortFieldMap
using PortMappingMap = std::map<std::string, PortFieldMap>;

// Slice specification for tensor_slice bindings (1D only, P0).
// 切片规格，用于 tensor_slice 绑定（P0 阶段仅支持一维张量）。
struct SliceSpec {
    int64_t start;   // start index (inclusive), must be >= 0
    int64_t length;  // number of elements, must be > 0
};

struct PackagingTarget {
    std::string name;
    std::string kind;   // scalar/tensor/tensor_element/tensor_slice
    std::string role;   // input/output
    std::vector<int> index;            // e.g. [0], used by tensor_element
    std::optional<SliceSpec> slice;    // used by tensor_slice; null for other kinds
};

struct ExpectedBinding {
    std::string sysmlPath;
    PackagingTarget target;

    std::string expectedDirection; // input/output
    std::string expectedType;      // double/int/bool...
    std::vector<int64_t> expectedShape; // could be empty to represent null
    std::string expectedPrecision; // float32/float64/int32...
};

struct GenerationOptions {
    std::string language = "C++";
    std::string cppStandard = "C++17";
    std::string nameSpace = "twin.generated";
    std::string className = "GeneratedWrapper";
    bool emitMappingTable = true;
    bool emitWarnings = true;
    bool emitUnmatchedList = true;
};

struct PackagingTaskRequestLite {
    std::string protocolVersion;
    std::string taskId;

    std::string blockKey;
    std::string blockName;

    std::string implementationType;   // ONNX / TensorFlow / PyTorch
    std::string implementationFilename;
    std::string deliveryPackageRelativePath;

    std::vector<ExpectedBinding> expectedBindings;
    GenerationOptions generationOptions;
    PortMappingMap portMapping;
};
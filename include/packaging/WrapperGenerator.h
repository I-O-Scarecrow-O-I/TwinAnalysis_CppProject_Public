#pragma once
#include "packaging/PackagingTypes.h"
#include "packaging/JsonHelpers.h"
#include "utils/json.hpp"
#include <string>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

struct GeneratedWrapperFiles {
    std::string headerPath;
    std::string sourcePath;

    // NEW: runtime files (self-contained wrapper, no dependency on AI_Adapter)
    std::string runtimeHeaderPath;
    std::string runtimeSourcePath;
};

class WrapperGenerator {
public:
    static GeneratedWrapperFiles generate(const PackagingTaskRequestLite& req,
        const nlohmann::ordered_json& detectedMeta,
        const std::string& outDirGeneratedSrc);

private:
    // Generate stub runtime + wrapper for non-ONNX frameworks (TF/PT).
    // The stub compiles but does not implement inference.
    static GeneratedWrapperFiles generateStub(const PackagingTaskRequestLite& req,
        const std::string& framework,
        const std::filesystem::path& incDir,
        const std::filesystem::path& srcDir,
        const std::string& cls,
        const std::string& nsCpp);

    // 新增：用于生成 PyTorch 真实包装器
    static GeneratedWrapperFiles generateTorch(const PackagingTaskRequestLite& req,
        const json& detectedMeta,
        const fs::path& incDir,
        const fs::path& srcDir,
        const std::string& cls,
        const std::string& nsCpp);
};

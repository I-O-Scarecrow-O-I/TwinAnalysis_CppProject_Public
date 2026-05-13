#include "ModelParser.h"
#include "utils/json.hpp"
#include "utils/ProcessRunner.h"

#include "packaging/RequestLoader.h"
#include "packaging/DetectedModelMetaBuilder.h"
#include "packaging/BindingChecker.h"
#include "packaging/WrapperGenerator.h"
#include "packaging/PackagingResultWriter.h"
#include "packaging/JsonHelpers.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static void usage() {
    std::cout << "AiModelPackagingCLI usage:\n"
        << "  AiModelPackagingCLI --request <request.json> --model <model.onnx|.pt|saved_model_dir> --out <out_dir> [--input-shape <dims>]\n"
        << "\n"
        << "  --input-shape <dims>  : (optional) comma‑separated input dimensions for PyTorch/TensorFlow models\n"
        << "                          e.g. --input-shape 1,2,10000\n"
        << "\nEnvironment variables:\n"
        << "  TF_PYTHON  Path to Python executable with TensorFlow installed (for TensorFlow models)\n"
        << "  PT_PYTHON  Path to Python executable with PyTorch installed (for PyTorch models)\n";
}

// Resolve the path to a meta-extraction Python script relative to the executable or a known location.
static std::string findMetaExtractScript(const std::string& scriptName, const std::string& exePath) {
    std::vector<fs::path> candidates;

    if (!exePath.empty()) {
        std::error_code ec;
        fs::path exeDir = fs::absolute(fs::path(exePath), ec).parent_path();
        if (!ec) {
            candidates.push_back(exeDir / "tools" / "meta_extract" / scriptName);
            candidates.push_back(exeDir.parent_path() / "tools" / "meta_extract" / scriptName);
        }
    }

    std::error_code cwdEc;
    const fs::path cwd = fs::current_path(cwdEc);
    if (!cwdEc) {
        candidates.push_back(cwd / "tools" / "meta_extract" / scriptName);
        candidates.push_back(cwd.parent_path() / "tools" / "meta_extract" / scriptName);
        candidates.push_back(cwd.parent_path().parent_path() / "tools" / "meta_extract" / scriptName);
    }

    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            return fs::weakly_canonical(c).string();
        }
    }
    return (fs::path("tools") / "meta_extract" / scriptName).string();
}



// Call a Python meta-extraction script and return the parsed JSON output.
// extraArgs : additional CLI arguments to pass to the script (e.g. --input-shape 1,2,10000)
static json callPythonMetaScript(
    const std::string& pythonExe,
    const std::string& scriptPath,
    const std::string& modelPath,
    const std::vector<std::string>& extraArgs,
    std::string& cmdStr,
    int& exitCode,
    std::string& stdoutStr,
    std::string& stderrStr,
    std::string& errorMsg)
{
    exitCode = -1;
    errorMsg.clear();

    std::vector<std::string> args = { pythonExe, scriptPath, modelPath };
    // Append extra arguments (e.g. --input-shape, 1,2,10000)
    for (const auto& a : extraArgs) {
        args.push_back(a);
    }

    cmdStr = ProcessRunner::argsToString(args);

    ProcessRunner::Result r = ProcessRunner::run(args);
    exitCode = r.exitCode;
    stdoutStr = r.stdOut;
    stderrStr = r.stdErr;

    if (!r.errorMsg.empty()) {
        errorMsg = "Failed to launch process: " + r.errorMsg;
        return json{};
    }

    // Try to parse stdout as JSON
    try {
        return json::parse(stdoutStr);
    }
    catch (const std::exception& e) {
        errorMsg = std::string("Failed to parse script JSON output: ") + e.what()
            + " | stdout=" + stdoutStr.substr(0, 200);
        return json{};
    }
}

int main(int argc, char* argv[]) {
    std::string requestPath;
    std::string modelPath;
    std::string outDir = "out";
    std::string inputShapeStr;   // 新增
    const std::string exePath = (argc > 0 && argv[0] != nullptr) ? argv[0] : "";
    const std::string cwdAtStart = fs::current_path().string();

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--request" && i + 1 < argc) requestPath = argv[++i];
        else if (a == "--model" && i + 1 < argc) modelPath = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--input-shape" && i + 1 < argc) inputShapeStr = argv[++i];
    }

    if (requestPath.empty() || modelPath.empty()) {
        usage();
        return 2;
    }

    // 1) Load request
    PackagingTaskRequestLite req;
    try {
        req = RequestLoader::loadFromFile(requestPath);
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load request: " << e.what() << "\n";
        return 1;
    }

    const std::string& implType = req.implementationType;
    const fs::path packagingRoot = fs::path(outDir) / "Packaging_Result";
    const fs::path genSrcDir = packagingRoot / "generated-src";

    // 2) Parse model metadata (branch by implementation type)
    json detectedMeta = json::object();
    json modelParserMeta = json::object(); // ONNX: ModelParser output
    json frameworkIoDump = json{};         // TF/PT: raw Python script JSON output
    bool parseOk = false;
    std::string parseMsg;

    // For framework_parse_log.txt
    std::string pythonExeUsed;
    std::string scriptPathUsed;
    std::string cmdStrUsed;
    int         pyExitCode = -1;
    std::string pyStdout;
    std::string pyStderr;

    // 在调用 callPythonMetaScript 之前
    std::vector<std::string> extraArgs;
    if (!inputShapeStr.empty()) {
        extraArgs.push_back("--input-shape");
        // 将逗号分隔的字符串拆分为多个参数，如 "1,2,10000" -> 1 2 10000
        std::stringstream ss(inputShapeStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            extraArgs.push_back(token);
        }
    }

    if (implType == "ONNX" || implType.empty()) {
#if TWIN_ENABLE_ONNX
        // --- ONNX: use existing ModelParser ---
        std::string metaJsonPath = ModelParser::parseAndSaveMetadata(modelPath);
        if (!metaJsonPath.empty()) {
            try {
                std::ifstream f(metaJsonPath);
                f >> modelParserMeta;
                parseOk = true;
            }
            catch (...) {
                parseOk = false;
                parseMsg = "Failed to read generated metajson";
            }
        }
        else {
            parseOk = false;
            parseMsg = "ModelParser failed to parse ONNX model";
        }

        if (parseOk) {
            if (modelParserMeta.empty() || !modelParserMeta.contains("inputs") || !modelParserMeta.contains("outputs")) {
                parseOk = false;
                parseMsg = "ModelParser produced incomplete metadata (missing inputs/outputs). Model may be invalid.";
            }
            else {
                try {
                    detectedMeta = DetectedModelMetaBuilder::buildFromModelParserMeta(modelParserMeta, "ONNX");
                }
                catch (const std::exception& e) {
                    parseOk = false;
                    parseMsg = std::string("Failed to build detected meta: ") + e.what();
                }
            }
        }
        else {
            detectedMeta["framework"] = "ONNX";
            detectedMeta["modelName"] = fs::path(modelPath).stem().string();
            detectedMeta["inputs"] = json::array();
            detectedMeta["outputs"] = json::array();
            detectedMeta["layers"] = json::array();
        }
#else
        parseOk = false;
        parseMsg = "ONNX support is disabled at build time (TWIN_ENABLE_ONNX=OFF)";
        detectedMeta["framework"] = "ONNX";
        detectedMeta["modelName"] = fs::path(modelPath).stem().string();
        detectedMeta["inputs"] = json::array();
        detectedMeta["outputs"] = json::array();
        detectedMeta["layers"] = json::array();
#endif
    }
    else if (implType == "TensorFlow") {
#if TWIN_ENABLE_TF && TWIN_ENABLE_ONNX
        // --- TensorFlow: try ONNX conversion first, then fall back to Python meta extraction ---
        const char* tfPython = std::getenv("TF_PYTHON");
        if (!tfPython || std::string(tfPython).empty()) {
            parseMsg = "TF_PYTHON environment variable not set or empty; cannot process TensorFlow model";
            parseOk = false;
        }
        else {
            // 1. 尝试将 TensorFlow 模型转换为 ONNX
            fs::path modelFsPath(modelPath);
            fs::path onnxOutputPath;

            // 生成 ONNX 输出路径：如果是目录则生成 <dir>/<dirname>.onnx，否则生成 <stem>.onnx
            if (fs::is_directory(modelFsPath)) {
                std::string dirName = modelFsPath.filename().string();
                onnxOutputPath = modelFsPath / (dirName + ".onnx");
            }
            else {
                onnxOutputPath = modelFsPath.parent_path() / (modelFsPath.stem().string() + ".onnx");
            }
            std::string onnxPathStr = onnxOutputPath.string();

            std::string converterArg = "--saved-model";
            if (!fs::is_directory(modelFsPath)) {
                const std::string ext = modelFsPath.extension().string();
                if (ext == ".h5" || ext == ".keras") converterArg = "--keras";
            }
            std::vector<std::string> convArgs = { tfPython, "-m", "tf2onnx.convert", converterArg, modelPath, "--output", onnxPathStr };
            ProcessRunner::Result convResult = ProcessRunner::run(convArgs);

            if (convResult.exitCode == 0 && fs::exists(onnxOutputPath)) {
                // 转换成功，后续按照 ONNX 模型处理
                std::cout << "[INFO] TensorFlow model successfully converted to ONNX: " << onnxPathStr << std::endl;

                // 修改模型路径和实现类型，后续流程将自动使用 ONNX 解析器和生成器
                modelPath = onnxPathStr;
                req.implementationType = "ONNX";   // 引用 implType 也会自动更新

                // 使用 ONNX ModelParser 解析
                std::string metaJsonPath = ModelParser::parseAndSaveMetadata(modelPath);
                if (!metaJsonPath.empty()) {
                    try {
                        std::ifstream f(metaJsonPath);
                        f >> modelParserMeta;
                        parseOk = true;
                    }
                    catch (...) {
                        parseOk = false;
                        parseMsg = "Failed to read generated metajson from ONNX model";
                    }
                }
                else {
                    parseOk = false;
                    parseMsg = "ModelParser failed to parse ONNX model";
                }

                if (parseOk) {
                    if (modelParserMeta.empty() || !modelParserMeta.contains("inputs") || !modelParserMeta.contains("outputs")) {
                        parseOk = false;
                        parseMsg = "ModelParser produced incomplete metadata (missing inputs/outputs). Model may be invalid.";
                    }
                    else {
                        try {
                            detectedMeta = DetectedModelMetaBuilder::buildFromModelParserMeta(modelParserMeta, "ONNX");
                        }
                        catch (const std::exception& e) {
                            parseOk = false;
                            parseMsg = std::string("Failed to build detected meta: ") + e.what();
                        }
                    }
                }
                else {
                    detectedMeta["framework"] = "ONNX";
                    detectedMeta["modelName"] = fs::path(modelPath).stem().string();
                    detectedMeta["inputs"] = json::array();
                    detectedMeta["outputs"] = json::array();
                    detectedMeta["layers"] = json::array();
                }

                // 记录转换命令信息，供日志使用
                pythonExeUsed = tfPython;
                scriptPathUsed = "tf2onnx.convert (auto)";
                cmdStrUsed = ProcessRunner::argsToString(convArgs);
                pyExitCode = convResult.exitCode;
                pyStdout = convResult.stdOut;
                pyStderr = convResult.stdErr;
            }
            else {
                // 转换失败，回退到原有的 TensorFlow 元数据提取（生成 stub）
                std::cerr << "[WARNING] ONNX conversion failed, falling back to TensorFlow metadata extraction.\n";
                if (!convResult.stdErr.empty())
                    std::cerr << "[WARNING] Conversion error: " << convResult.stdErr << std::endl;

                pythonExeUsed = tfPython;
                scriptPathUsed = findMetaExtractScript("extract_tensorflow_meta.py", exePath);

                std::string errorMsg;
                json scriptJson = callPythonMetaScript(
                    pythonExeUsed, scriptPathUsed, modelPath,
                    extraArgs,   // extraArgs 已在 main 函数中统一构建（可能为空）
                    cmdStrUsed, pyExitCode, pyStdout, pyStderr, errorMsg);

                if (!errorMsg.empty()) {
                    parseMsg = errorMsg;
                    parseOk = false;
                }
                else if (pyExitCode != 0 || (scriptJson.contains("status") && scriptJson["status"] == "FAILED")) {
                    parseMsg = scriptJson.value("message", "TensorFlow meta-extraction script returned FAILED (exit code "
                        + std::to_string(pyExitCode) + ")");
                    parseOk = false;
                    frameworkIoDump = scriptJson;
                }
                else {
                    parseOk = true;
                    frameworkIoDump = scriptJson;
                    detectedMeta = DetectedModelMetaBuilder::buildFromPythonScriptMeta(
                        scriptJson, "TensorFlow", fs::path(modelPath).stem().string());
                }
            }
        }

        // 若 parseOk 仍为 false 且 detectedMeta 为空，填入基本框架信息
        if (!parseOk && detectedMeta.empty()) {
            detectedMeta["framework"] = "TensorFlow";
            detectedMeta["modelName"] = fs::path(modelPath).stem().string();
            detectedMeta["inputs"] = json::array();
            detectedMeta["outputs"] = json::array();
            detectedMeta["layers"] = json::array();
        }
#elif !TWIN_ENABLE_TF
        parseOk = false;
        parseMsg = "TensorFlow support is disabled at build time (TWIN_ENABLE_TF=OFF)";
        detectedMeta["framework"] = "TensorFlow";
        detectedMeta["modelName"] = fs::path(modelPath).stem().string();
        detectedMeta["inputs"] = json::array();
        detectedMeta["outputs"] = json::array();
        detectedMeta["layers"] = json::array();
#else
        parseOk = false;
        parseMsg = "TensorFlow workflow requires ONNX support (TWIN_ENABLE_ONNX=ON)";
        detectedMeta["framework"] = "TensorFlow";
        detectedMeta["modelName"] = fs::path(modelPath).stem().string();
        detectedMeta["inputs"] = json::array();
        detectedMeta["outputs"] = json::array();
        detectedMeta["layers"] = json::array();
#endif
    }
    else if (implType == "PyTorch") {
#if TWIN_ENABLE_TORCH
        // --- PyTorch: call Python meta-extraction script ---
        const char* ptPython = std::getenv("PT_PYTHON");
        if (!ptPython || std::string(ptPython).empty()) {
            parseMsg = "PT_PYTHON environment variable not set or empty; cannot extract PyTorch model metadata";
            parseOk = false;
        }
        else {
            pythonExeUsed = ptPython;
            scriptPathUsed = findMetaExtractScript("extract_pytorch_meta.py", exePath);

            std::string errorMsg;
            json scriptJson = callPythonMetaScript(
                pythonExeUsed, scriptPathUsed, modelPath,
                extraArgs,
                cmdStrUsed, pyExitCode, pyStdout, pyStderr, errorMsg);
            if (!pyStderr.empty()) {
                std::cerr << pyStderr;
            }

            if (!errorMsg.empty()) {
                parseMsg = errorMsg;
                parseOk = false;
            }
            else if (pyExitCode != 0 || (scriptJson.contains("status") && scriptJson["status"] == "FAILED")) {
                parseMsg = scriptJson.value("message", "PyTorch meta-extraction script returned FAILED (exit code "
                    + std::to_string(pyExitCode) + ")");
                parseOk = false;
                frameworkIoDump = scriptJson;
            }
            else {
                parseOk = true;
                frameworkIoDump = scriptJson;
                detectedMeta = DetectedModelMetaBuilder::buildFromPythonScriptMeta(
                    scriptJson, "PyTorch", fs::path(modelPath).stem().string());
            }
        }

        if (!parseOk && detectedMeta.empty()) {
            detectedMeta["framework"] = "PyTorch";
            detectedMeta["modelName"] = fs::path(modelPath).stem().string();
            detectedMeta["inputs"] = json::array();
            detectedMeta["outputs"] = json::array();
            detectedMeta["layers"] = json::array();
        }
#else
        parseOk = false;
        parseMsg = "PyTorch support is disabled at build time (TWIN_ENABLE_TORCH=OFF)";
        detectedMeta["framework"] = "PyTorch";
        detectedMeta["modelName"] = fs::path(modelPath).stem().string();
        detectedMeta["inputs"] = json::array();
        detectedMeta["outputs"] = json::array();
        detectedMeta["layers"] = json::array();
#endif
    }
    else {
        parseOk = false;
        parseMsg = "Unsupported implementation.type: " + implType;
        detectedMeta["framework"] = implType;
        detectedMeta["modelName"] = fs::path(modelPath).stem().string();
        detectedMeta["inputs"] = json::array();
        detectedMeta["outputs"] = json::array();
        detectedMeta["layers"] = json::array();
    }

    // 3) Checks based on expectedBindings
    BindingCheckOutputs checks = BindingChecker::runChecks(req, detectedMeta);

    // 4) Generate wrapper (TF/PT -> stub; ONNX -> full)
    GeneratedWrapperFiles wrapperFiles{};
    bool wrapperOk = true;
    try {
        wrapperFiles = WrapperGenerator::generate(req, detectedMeta, genSrcDir.string());
    }
    catch (const std::exception& e) {
        wrapperOk = false;
        json w = json::object();
        w["code"] = "WRAPPER_GENERATION_FAILED";
        w["severity"] = "ERROR";
        w["path"] = "";
        w["message"] = e.what();
        checks.warnings.push_back(w);
    }

    // For non-ONNX: add a warning that inference is not implemented
    if (implType == "TensorFlow") {   // 仅 TF 生成 stub 时添加警告
        json w = json::object();
        w["code"] = "INFERENCE_NOT_IMPLEMENTED";
        w["severity"] = "WARN";
        w["path"] = "";
        w["message"] = implType + " inference is not implemented in the generated wrapper stub. "
            "The generated source compiles but step() will return false. "
            "Integrate the actual " + implType + " C++ runtime to enable inference.";
        checks.warnings.push_back(w);
    }

    bool generatedSrcOk = false;
    if (wrapperOk) {
        generatedSrcOk =
            !wrapperFiles.headerPath.empty() &&
            !wrapperFiles.sourcePath.empty() &&
            fs::exists(wrapperFiles.headerPath) &&
            fs::exists(wrapperFiles.sourcePath);
    }

    if (!generatedSrcOk) {
        json w = json::object();
        w["code"] = "GENERATED_SOURCE_MISSING";
        w["severity"] = "ERROR";
        w["path"] = "generated-src";
        w["message"] = "Required generated wrapper source/header files are missing.";
        checks.warnings.push_back(w);
    }

    // 5) Result_Metadata.json
    json resultMeta;
    resultMeta["protocolVersion"] = "1.0";
    resultMeta["taskId"] = req.taskId;
    resultMeta["service"] = "AiModelPackagingService";
    resultMeta["blockKey"] = req.blockKey;
    resultMeta["blockName"] = req.blockName;
    resultMeta["modelType"] = implType.empty() ? "ONNX" : implType;
    if (generatedSrcOk) {
        resultMeta["wrapperClassName"] = req.generationOptions.nameSpace + "::" + req.generationOptions.className;
    }
    else {
        resultMeta["wrapperClassName"] = nullptr;
    }
    resultMeta["generatedAt"] = pkg::now_iso8601_local();

    if (!parseOk || !wrapperOk || !generatedSrcOk) {
        resultMeta["status"] = "FAILED";
        if (!parseOk) {
            resultMeta["message"] = parseMsg.empty() ? "Model parse failed" : parseMsg;
        }
        else if (!wrapperOk) {
            resultMeta["message"] = "Wrapper generation failed";
        }
        else {
            resultMeta["message"] = "Required generated source files missing";
        }
    }
    else {
        bool anyFail = false;
        for (auto& c : checks.compatibilityChecks) {
            if (c.contains("directionResult") && c["directionResult"] == "FAIL") anyFail = true;
            if (c.contains("typeResult") && c["typeResult"] == "FAIL") anyFail = true;
            if (c.contains("shapeResult") && c["shapeResult"] == "FAIL") anyFail = true;
            if (c.contains("precisionResult") && c["precisionResult"] == "FAIL") anyFail = true;
        }
        resultMeta["status"] = anyFail ? "PARTIAL_SUCCESS" : "SUCCEEDED";
        resultMeta["message"] = anyFail ? "Parsed model, wrapper generated, some checks failed" : "Wrapper generated successfully";
    }

    // 6) Build framework_parse_log.txt
    std::ostringstream logStream;
    logStream << "=== Framework Parse Log ===\n";
    logStream << "generatedAt: " << pkg::now_iso8601_local() << "\n";
    logStream << "parseOk: " << (parseOk ? "true" : "false") << "\n";
    logStream << "parseMsg: " << (parseMsg.empty() ? "(none)" : parseMsg) << "\n";
    logStream << "implementationType: " << implType << "\n";
    logStream << "cwdAtStart: " << cwdAtStart << "\n";
    logStream << "cwdNow: " << fs::current_path().string() << "\n";
    logStream << "modelPath: " << modelPath << "\n";
    logStream << "modelExists: " << (fs::exists(modelPath) ? "true" : "false") << "\n";
    logStream << "requestPath: " << requestPath << "\n";
    logStream << "requestExists: " << (fs::exists(requestPath) ? "true" : "false") << "\n";
    logStream << "wrapperGenerated: " << (wrapperOk ? "true" : "false") << "\n";
    logStream << "generatedSourceExists: " << (generatedSrcOk ? "true" : "false") << "\n";
    if (implType == "ONNX" || implType.empty()) {
        // ONNX path: log the ModelParser metajson path (not stored in a variable here, but log modelParserMeta status)
        logStream << "onnxMetaJsonParsed: " << (!modelParserMeta.empty() ? "true" : "false") << "\n";
    }
    else {
        // TF/PT path: log python invocation details
        logStream << "pythonExecutable: " << (pythonExeUsed.empty() ? "(not set)" : pythonExeUsed) << "\n";
        logStream << "scriptPath: " << (scriptPathUsed.empty() ? "(none)" : scriptPathUsed) << "\n";
        if (!scriptPathUsed.empty() && scriptPathUsed != "tf2onnx.convert (auto)") {
            logStream << "scriptExists: " << (fs::exists(scriptPathUsed) ? "true" : "false") << "\n";
        }
        logStream << "command: " << (cmdStrUsed.empty() ? "(not run)" : cmdStrUsed) << "\n";
        logStream << "exitCode: " << pyExitCode << "\n";
        if (!pyStderr.empty()) {
            logStream << "stderrLength: " << pyStderr.size() << " bytes\n";
            logStream << "--- stderr summary (first 2048 chars) ---\n";
            logStream << pyStderr.substr(0, std::min<size_t>(2048, pyStderr.size())) << "\n";
            logStream << "--- end stderr ---\n";
        }
        if (!pyStdout.empty()) {
            // stdout may be large JSON; summarise
            logStream << "stdoutLength: " << pyStdout.size() << " bytes\n";
        }
    }
    const std::string frameworkParseLogText = logStream.str();

    // 7) Write all required files + raw-extract + docs
    PackagingResultWriter::writeAll(req,
        packagingRoot.string(),
        resultMeta,
        detectedMeta,
        checks.mappingTable,
        checks.unmatchedItems,
        checks.warnings,
        checks.compatibilityChecks,
        modelParserMeta,
        requestPath,
        modelPath,
        frameworkParseLogText,
        frameworkIoDump
    );

    std::cout << "Packaging result generated at: " << packagingRoot.string() << "\n";
    if (!wrapperFiles.headerPath.empty()) {
        std::cout << "Generated wrapper:\n  " << wrapperFiles.headerPath << "\n  " << wrapperFiles.sourcePath << "\n";
    }
    if (!parseOk || !wrapperOk || !generatedSrcOk) {
        if (!parseOk) {
            std::cerr << "WARNING: Model parse FAILED: " << parseMsg << "\n";
        }
        else if (!wrapperOk) {
            std::cerr << "WARNING: Wrapper generation FAILED.\n";
        }
        else {
            std::cerr << "WARNING: Generated wrapper files are missing.\n";
        }
        std::cerr << "Package produced with status=FAILED.\n";
    }

    return 0;
}

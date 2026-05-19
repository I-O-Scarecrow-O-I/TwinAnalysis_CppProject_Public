#include <httplib.h>
#include <utils/json.hpp>
#include <utils/ProcessRunner.h>
#include <picosha2.h>
#include "ResultUploader.h"
#include "ServiceConfig.h"

#include <fstream>
#include <filesystem>
#include <cstdio>
#include <vector>
#include <string>
#include <thread>
#include <sstream>
#include <iomanip>
#include<cstdint>

#include <mz.h>
#include <mz_zip.h>
#include <mz_strm_os.h>    // 文件流
#include <mz_strm.h>     // 使用 Z_DEFLATED 等宏
#include <mz_zip_rw.h>
#include <cstdlib>   // std::getenv

#ifdef _WIN32
#include <windows.h>
#endif


// ========== 宏配置（由 CMake 注入） ==========
#ifndef PROJECT_ROOT
#error "PROJECT_ROOT must be defined by CMake"
#endif

#ifndef PACKAGING_CLI_CMAKE_PATH
#error "PACKAGING_CLI_CMAKE_PATH is not defined. Inject it from CMake via target_compile_definitions()."
#endif

#ifndef PACKAGING_CLI_DOCKER_PATH
#error "PACKAGING_CLI_DOCKER_PATH is not defined. Inject /opt/twin/bin/AiModelPackagingCLI."
#endif

#ifndef MICROSERVICE_HOST
#define MICROSERVICE_HOST "0.0.0.0"
#endif

#ifndef MICROSERVICE_PORT
#define MICROSERVICE_PORT "8080"
#endif

#ifndef ADP_UPLOAD_BASE_URL
#define ADP_UPLOAD_BASE_URL "http://192.168.0.40:8080"
#endif

#ifndef ADP_UPLOAD_PATH
#define ADP_UPLOAD_PATH "/api/v1/files/upload"
#endif

#ifndef ADP_CALLBACK_BASE_URL
#define ADP_CALLBACK_BASE_URL "http://192.168.0.40:33000"
#endif

#ifndef ADP_CALLBACK_PATH
#define ADP_CALLBACK_PATH "/internal/module2/callbacks/wrapper-codegen"
#endif

#ifndef DEFAULT_UPLOAD_DIR_NAME
#define DEFAULT_UPLOAD_DIR_NAME "Upload_Result"
#endif

#ifndef TWIN_ENABLE_ADP_CALLBACK
#define TWIN_ENABLE_ADP_CALLBACK 0
#endif

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

static std::string getEnvOrEmpty(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

static void setWorkingDirectoryOrWarn() {
    namespace fs = std::filesystem;
    std::error_code ec;

    // 1) runtime override
    const std::string workdir = getEnvOrEmpty("TWIN_WORKDIR");
    if (!workdir.empty()) {
        fs::create_directories(fs::path(workdir), ec); // ensure exists
        ec.clear();
        fs::current_path(fs::path(workdir), ec);
        if (!ec) {
            std::cout << "[BOOT] Working directory set to TWIN_WORKDIR=" << workdir << "\n";
            return;
        }
        std::cerr << "[WARN] Failed to set working dir to TWIN_WORKDIR=" << workdir
            << " err=" << ec.message() << "\n";
    }

    // 2) fallback to PROJECT_ROOT
    ec.clear();
    fs::current_path(fs::path(PROJECT_ROOT), ec);
    if (!ec) {
        std::cout << "[BOOT] Working directory set to PROJECT_ROOT=" << PROJECT_ROOT << "\n";
        return;
    }

    // 3) final fallback: keep current dir
    std::cerr << "[WARN] Failed to set working dir to PROJECT_ROOT=" << PROJECT_ROOT
        << " err=" << ec.message() << ". Continue with current working dir.\n";
}

// ========== 行业规范：端口解析要可诊断 ==========
static int parsePortOrDie(const std::string& s) {
    auto trim = [](std::string v) {
        size_t b = v.find_first_not_of(" \t\r\n\"");
        size_t e = v.find_last_not_of(" \t\r\n\"");
        if (b == std::string::npos) return std::string{};
        return v.substr(b, e - b + 1);
        };

    std::string t = trim(s);
    if (t.empty()) throw std::invalid_argument("empty port");
    for (char c : t) {
        if (c < '0' || c > '9') throw std::invalid_argument("non-numeric port: " + t);
    }
    int p = std::stoi(t);
    if (p < 1 || p > 65535) throw std::invalid_argument("port out of range: " + std::to_string(p));
    return p;
}

// ========== 新增：artifact 下载接口（ADP 会用 artifactUri 下载 ZIP） ==========
void handleDownloadArtifact(const httplib::Request& req, httplib::Response& res) {
    const std::string root = PROJECT_ROOT;

    std::string artifactId = req.matches[1];

    // 安全：禁止目录穿越
    if (artifactId.find("..") != std::string::npos || artifactId.find('\\') != std::string::npos) {
        res.status = 400;
        res.set_content(R"({"error":"invalid artifactId"})", "application/json");
        return;
    }

    fs::path zipPath = fs::path(root) / "Upload_Result" / artifactId;
    if (!fs::exists(zipPath)) {
        res.status = 404;
        res.set_content(R"({"error":"artifact not found"})", "application/json");
        return;
    }

    std::ifstream f(zipPath.string(), std::ios::binary);
    if (!f.is_open()) {
        res.status = 500;
        res.set_content(R"({"error":"failed to open artifact"})", "application/json");
        return;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string body = ss.str();

    res.status = 200;
    res.set_header("Content-Disposition", ("attachment; filename=\"" + artifactId + "\"").c_str());
    res.set_content(std::move(body), "application/zip");
}

// ========== 新增：回调 ADP（不鉴权；失败不阻断本地流程） ==========
bool callbackAdpWrapperCodegen(const std::string& taskId,
    bool success,
    const std::string& artifactUri,
    const std::string& artifactSha256,
    const std::string& wrapperClass,
    const std::string& modelType,
    const std::string& runtime,
    int fileCount,
    const json& warnings,
    std::string& outErr)
{
    outErr.clear();

#if (TWIN_ENABLE_ADP_CALLBACK == 0)
    (void)taskId; (void)success; (void)artifactUri; (void)artifactSha256;
    (void)wrapperClass; (void)modelType; (void)runtime; (void)fileCount; (void)warnings;
    std::cout << "[ADP] callback disabled.\n";
    return true;
#else
    const std::string base = g_config.callbackBaseUrl;
    if (base.empty()) {
        outErr = "ADP_CALLBACK_BASE_URL_ is empty";
        return false;
    }

    json body;

    // 文档 taskId 是 int64
    body["taskId"] = std::stoll(taskId);
    body["success"] = success;
    body["status"] = success ? "SUCCESS" : "FAILED";
    body["projectName"] = "TwinAnalysis";
    body["artifactUri"] = artifactUri;           // 关键：这里是 upload 接口返回的 downloadUrl
    body["artifactType"] = "zip";
    body["artifactSha256"] = artifactSha256;
    body["mainClassName"] = "";
    body["wrapperClass"] = wrapperClass;
    body["modelType"] = modelType;
    body["runtime"] = runtime;
    body["modelCount"] = 1;
    body["fileCount"] = fileCount;
    body["files"] = json::object();
    body["warnings"] = warnings;
    body["errorCode"] = "";
    body["errorMessage"] = "";

    std::cout << "[ADP] POST " << base << g_config.callbackPath << "\n";
    std::cout << "[ADP] body: " << body.dump(2) << "\n";

    httplib::Client cli(base);
    cli.set_connection_timeout(10);
    cli.set_read_timeout(30);
    cli.set_write_timeout(30);

    auto resp = cli.Post(g_config.callbackPath, body.dump(), "application/json");
    if (!resp) {
        outErr = "callback request failed (no response)";
        return false;
    }
    if (resp->status != 200) {
        outErr = "HTTP " + std::to_string(resp->status) + " body=" + resp->body;
        return false;
    }

    std::cout << "[ADP] callback OK: " << resp->body << "\n";
    return true;
#endif
}

static std::string getCwdString() {
    std::error_code ec;
    auto p = std::filesystem::current_path(ec);
    if (ec) return "(unknown)";
    return p.string();
}

// 使用 minizip-ng 压缩文件夹
void zipFolder(const fs::path& rootDir, const fs::path& outputZip) {
    // 创建 zip 句柄
    void* zipHandle = mz_zip_create();
    if (!zipHandle) throw std::runtime_error("mz_zip_create failed");

    // 创建文件流
    void* fileStream = mz_stream_os_create();
    if (!fileStream) {
        mz_zip_delete(&zipHandle);
        throw std::runtime_error("mz_stream_os_create failed");
    }

    // 打开文件流（写入模式）
    int32_t err = mz_stream_os_open(fileStream, outputZip.string().c_str(),
        MZ_OPEN_MODE_CREATE | MZ_OPEN_MODE_WRITE);
    if (err != MZ_OK) {
        mz_stream_os_delete(&fileStream);
        mz_zip_delete(&zipHandle);
        throw std::runtime_error("Cannot open output file stream");
    }

    // 将流与 zip 关联
    err = mz_zip_open(zipHandle, fileStream, MZ_OPEN_MODE_CREATE | MZ_OPEN_MODE_WRITE);
    if (err != MZ_OK) {
        mz_stream_os_close(fileStream);
        mz_stream_os_delete(&fileStream);
        mz_zip_delete(&zipHandle);
        throw std::runtime_error("mz_zip_open failed");
    }

    for (const auto& entry : fs::recursive_directory_iterator(rootDir)) {
        if (entry.is_directory()) continue;

        //fs::path relativePath = fs::relative(entry.path(), rootDir.parent_path());
        fs::path relativePath = fs::relative(entry.path(), rootDir);
        std::string pathInZip = relativePath.string();
        std::replace(pathInZip.begin(), pathInZip.end(), '\\', '/');

        // 设置文件信息
        mz_zip_file fileInfo = {};
        fileInfo.filename = pathInZip.c_str();
        fileInfo.modified_date = time(nullptr);
        //fileInfo.version_madeby = MZ_VERSION_MADEBY;

        err = mz_zip_entry_write_open(zipHandle, &fileInfo, MZ_COMPRESS_LEVEL_DEFAULT, 0, nullptr);
        if (err != MZ_OK) {
            mz_zip_close(zipHandle);
            mz_stream_os_close(fileStream);
            mz_stream_os_delete(&fileStream);
            mz_zip_delete(&zipHandle);
            throw std::runtime_error("mz_zip_entry_write_open failed");
        }

        std::ifstream ifs(entry.path(), std::ios::binary);
        std::vector<char> buf(65536);
        while (ifs) {
            ifs.read(buf.data(), buf.size());
            if (ifs.gcount() > 0) {
                int32_t written = mz_zip_entry_write(zipHandle, buf.data(), ifs.gcount());
                if (written != ifs.gcount()) {
                    mz_zip_entry_close(zipHandle);
                    mz_zip_close(zipHandle);
                    mz_stream_os_close(fileStream);
                    mz_stream_os_delete(&fileStream);
                    mz_zip_delete(&zipHandle);
                    throw std::runtime_error("mz_zip_entry_write failed");
                }
            }
        }
        mz_zip_entry_close(zipHandle);
    }

    mz_zip_close(zipHandle);
    mz_stream_os_close(fileStream);
    mz_stream_os_delete(&fileStream);
    mz_zip_delete(&zipHandle);
}

void unzipFolder(const std::filesystem::path& zipPath, const std::filesystem::path& destDir) {
    // 确保目标目录存在
    std::filesystem::create_directories(destDir);

    // ========== 首先尝试使用 minizip-ng API ==========
    try {
        void* reader = mz_zip_reader_create();
        if (!reader) {
            throw std::runtime_error("mz_zip_reader_create failed");
        }

        int32_t err = mz_zip_reader_open_file(reader, zipPath.string().c_str());
        if (err != MZ_OK) {
            mz_zip_reader_delete(&reader);
            throw std::runtime_error("Cannot open zip file for reading: " + zipPath.string());
        }

        err = mz_zip_reader_save_all(reader, destDir.string().c_str());
        if (err != MZ_OK) {
            mz_zip_reader_close(reader);
            mz_zip_reader_delete(&reader);
            throw std::runtime_error("mz_zip_reader_save_all failed for: " + zipPath.string());
        }

        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);

        std::cout << "[UNZIP] Successfully extracted using minizip-ng API." << std::endl;
        return;   // 成功，直接返回
    } catch (const std::exception& e) {
        std::cerr << "[UNZIP] minizip-ng failed: " << e.what() << ". Falling back to system unzip command." << std::endl;
    }

    // ========== 回退方案：调用系统 unzip 命令 ==========
    std::string command = "unzip -o \"" + zipPath.string() + "\" -d \"" + destDir.string() + "\"";
    std::cout << "[UNZIP] Executing system command: " << command << std::endl;
    int ret = system(command.c_str());
    if (ret != 0) {
        throw std::runtime_error("Both minizip-ng and system unzip failed. unzip exit code: " + std::to_string(ret));
    }
    std::cout << "[UNZIP] Successfully extracted using system unzip command." << std::endl;
}

// SHA256 计算（使用 picosha2）
std::string sha256File(const std::string& filePath) {
    std::ifstream ifs(filePath, std::ios::binary);
    std::vector<unsigned char> hash(picosha2::k_digest_size);
    picosha2::hash256(ifs, hash.begin(), hash.end());
    std::ostringstream oss;
    for (auto c : hash) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return oss.str();
}

// ========== 工具函数 ==========
// 使用 cpp-httplib 下载文件
bool downloadFile(const std::string& url, const std::string& localPath,
    const std::string& expectedSha256,
    std::string& finalModelPath) {
    // 转义路径
    std::string escapedPath = localPath;
    std::replace(escapedPath.begin(), escapedPath.end(), '\\', '/');

    bool isZip = (localPath.size() > 4 && localPath.substr(localPath.size() - 4) == ".zip");

    if (isZip) {
        // --- ZIP 处理：下载 → 校验 → 解压 → 删除 ZIP ---
        std::string zipPath = localPath;                              // models/xxx.zip
        std::string targetDir = localPath.substr(0, localPath.size() - 4); // models/xxx

        // 1) 下载 ZIP
        std::string cmd = "curl -L --connect-timeout 10 --max-time 120 -o \"" + escapedPath + "\" \"" + url + "\"";
        std::cout << "[DOWNLOAD] Downloading ZIP: " << cmd << std::endl;
        int ret = system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "[DOWNLOAD] curl failed with code " << ret << std::endl;
            return false;
        }

        // 2) SHA256 校验（若提供）
        if (!expectedSha256.empty()) {
            std::string actualSha = sha256File(zipPath);
            if (actualSha != expectedSha256) {
                // 在服务启动时打印：
#ifdef _WIN32
                std::cerr << "[BOOT] PID=" << GetCurrentProcessId() << std::endl;
#endif
                std::cerr << "[PACKAGING] SHA256 mismatch!\n"
                    << "  expectedSha256: " << expectedSha256 << "\n"
                    << "  actualSha256:   " << actualSha << "\n"
                    << "  zipPath:      " << zipPath << "\n"
                    << "  fileSize:       " << std::filesystem::file_size(zipPath) << "\n"
                    << std::endl;
                return false;
            }
            std::cout << "[PACKAGING] SHA256 verification passed." << std::endl;
        }
        else {
            std::cout << "[PACKAGING] No contentSha256 provided, skipping integrity check." << std::endl;
        }

        // 3) 解压到目标目录
        try {
            std::filesystem::create_directories(targetDir);
            unzipFolder(zipPath, targetDir);
        }
        catch (const std::exception& e) {
            std::cerr << "[DOWNLOAD] unzip failed: " << e.what() << std::endl;
            return false;
        }

        // 4) 如果解压后只有一个子目录，则上移其内容
        {
            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(targetDir)) {
                entries.push_back(entry.path());
            }
            if (entries.size() == 1 && std::filesystem::is_directory(entries[0])) {
                std::filesystem::path singleDir = entries[0];
                for (const auto& inner : std::filesystem::directory_iterator(singleDir)) {
                    std::filesystem::rename(inner.path(), targetDir / inner.path().filename());
                }
                std::filesystem::remove(singleDir);
                std::cout << "[DOWNLOAD] Removed extra top-level directory: " << singleDir.filename() << std::endl;
            }
        }

        // 5) 删除 ZIP 文件
        std::filesystem::remove(zipPath);
        std::cout << "[DOWNLOAD] ZIP extracted to: " << targetDir << std::endl;
        finalModelPath = targetDir;
        return true;

    }
    else {
        // --- 普通单文件下载 ---
        std::string command = "curl -L --connect-timeout 10 --max-time 60 -o \"" + escapedPath + "\" \"" + url + "\"";
        std::cout << "[DOWNLOAD] Executing: " << command << std::endl;
        int ret = system(command.c_str());
        if (ret != 0) {
            std::cerr << "[DOWNLOAD] curl failed with code " << ret << std::endl;
            return false;
        }

        // 完整性校验（如果请求中提供了 contentSha256）
        if (!expectedSha256.empty()) {
            std::string actualSha = sha256File(localPath);
            if (actualSha != expectedSha256) {
                // 在服务启动时打印：
#ifdef _WIN32
                std::cerr << "[BOOT] PID=" << GetCurrentProcessId() << std::endl;
#endif
                std::cerr << "[PACKAGING] SHA256 mismatch!\n"
                    << "  expectedSha256: " << expectedSha256 << "\n"
                    << "  actualSha256:   " << actualSha << "\n"
                    << "  localPath:      " << localPath << "\n"
                    << "  fileSize:       " << std::filesystem::file_size(localPath) << "\n"
                    << std::endl;
                return false;
            }
            std::cout << "[PACKAGING] SHA256 verification passed." << std::endl;
        }
        else {
            std::cout << "[PACKAGING] No contentSha256 provided, skipping integrity check." << std::endl;
        }
        

        // 验证文件非空
        std::ifstream ifs(localPath, std::ios::binary | std::ios::ate);
        if (!ifs || ifs.tellg() == 0) {
            std::cerr << "[DOWNLOAD] Downloaded file is empty or not found." << std::endl;
            return false;
        }
        std::cout << "[DOWNLOAD] Downloaded file size: " << ifs.tellg() << " bytes" << std::endl;
        finalModelPath = localPath;
        return true;
    }
}

// 调用 AiModelPackagingCLI（跨平台）
bool runPackagingCLI(const std::string& requestFile,
    const std::string& modelPath,
    const std::string& outDir,
    const std::string& inputShape)
{
    namespace fs = std::filesystem;

    const char* envCli = std::getenv("TWIN_PACKAGING_CLI_DOCKER_PATH");
    std::string cliPath;
    if (envCli && std::strlen(envCli) > 0) {
        cliPath = envCli;
    }
    else {
        cliPath = PACKAGING_CLI_CMAKE_PATH;   // CMake 注入的编译宏
    }

    // 严格：不查找、不fallback，只校验 CMake 注入的路径是否存在
    std::error_code ec;
    if (!fs::exists(cliPath, ec)) {
        std::cerr << "[CLI] CLI not found at build-defined path: " << cliPath << "\n"
            << "[CLI] cwd=" << getCwdString() << "\n";
        return false;
    }

    std::vector<std::string> args = {
        cliPath,
        "--request", requestFile,
        "--model", modelPath,
        "--out", outDir,
    };

    // 可选参数：input-shape
    if (!inputShape.empty()) {
        args.push_back("--input-shape");
        args.push_back(inputShape);
    }

    std::cout << "[CLI] Running: " << ProcessRunner::argsToString(args) << "\n";
    std::cout << "[CLI] cwd=" << getCwdString() << "\n";

    ProcessRunner::Result result = ProcessRunner::run(args);
    if (!result.errorMsg.empty()) {
        std::cerr << "[CLI] Launch failed: " << result.errorMsg << "\n";
        return false;
    }

    if (!result.stdOut.empty()) std::cout << result.stdOut << std::flush;
    if (!result.stdErr.empty()) std::cerr << result.stdErr << std::flush;
    std::cout << "[CLI] Exit code: " << result.exitCode << "\n";
    return result.exitCode == 0;
}

void collectRuntimeFiles(const std::string& resultDir,
    const std::string& framework,
    const std::string& destDir) {
    namespace fs = std::filesystem;
    std::string packResultDir = resultDir + "/Packaging_Result";

    // 创建目录
    fs::create_directories(destDir + "/generated-src/include");
    fs::create_directories(destDir + "/generated-src/src");
    fs::create_directories(destDir + "/include/utils");

    // 辅助 lambda：安全复制文件
    auto safeCopyFile = [](const fs::path& src, const fs::path& dst) {
        if (fs::exists(src)) {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
        else {
            std::cerr << "[WARN] Source file not found, skip copy: " << src << std::endl;
        }
        };

    // 1. 复制 generated-src（目录复制仍使用原有方式，但可加 try-catch）
    try {
        fs::copy(packResultDir + "/generated-src/include", destDir + "/generated-src/include",
            fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        fs::copy(packResultDir + "/generated-src/src", destDir + "/generated-src/src",
            fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "[WARN] Failed to copy generated-src: " << e.what() << std::endl;
    }

    // 2. 复制 IModelBlock.h
    safeCopyFile(std::string(PROJECT_ROOT) + "/include/IModelBlock.h",
        destDir + "/include/IModelBlock.h");

    // 3. 复制 json.hpp
    safeCopyFile(std::string(PROJECT_ROOT) + "/include/utils/json.hpp",
        destDir + "/include/utils/json.hpp");

    // 4. 复制 Detected_Model_Meta.json
    safeCopyFile(packResultDir + "/Detected_Model_Meta.json",
        destDir + "/Detected_Model_Meta.json");

    // 框架特定的运行时文件（如果不在 generated-src 中）
    // 一般情况下 ONNX 的 OnnxModelRuntime.h/.cpp 已在 generated-src 中，
    // PyTorch 的 TorchModelRuntime.h/.cpp 同理。如果缺失，可在此补充。
    // 当前暂无额外文件需要复制。

    // 注意：不复制 generated-docs/ 和 raw-extract/
}

// ========== HTTP 处理函数 ==========
void handlePackaging(const httplib::Request& req, httplib::Response& res) {
    json requestBody;
    try {
        requestBody = json::parse(req.body);
    }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"Invalid JSON"})", "application/json");
        return;
    }

    // --- 原有逻辑：从请求体读字段 ---
    std::string taskId = requestBody.value("taskId", "unknown");
    std::string blockName = requestBody["block"].value("blockName", "unnamed");
    std::string implType = requestBody["implementation"].value("type", "ONNX");
    std::string fileUri = requestBody["implementation"].value("fileUri", "");
    std::string implFilename = requestBody["implementation"].value("filename", "model.onnx");
    // 可选参数：input-shape
    std::string inputShape = "";
    if (requestBody.contains("expectedBindings") &&
        requestBody["expectedBindings"].is_array() &&
        !requestBody["expectedBindings"].empty() &&
        requestBody["expectedBindings"][0].contains("inputShape") &&
        requestBody["expectedBindings"][0]["inputShape"].is_string()) {
        inputShape = requestBody["expectedBindings"][0]["inputShape"].get<std::string>();
    }

    // 使用项目根目录（由 CMake 注入的 PROJECT_ROOT 宏）
    std::string root = PROJECT_ROOT;
    std::filesystem::create_directories(root + "/models");
    std::filesystem::create_directories(root + "/requests");
    std::filesystem::create_directories(root + "/Upload_Result");

    try {
        std::stoll(taskId);
    }
    catch (...) {
        res.status = 400;
        res.set_content(R"({"error":"taskId must be a valid integer"})", "application/json");
        return;
    }

    // 1. 下载模型（自动处理 ZIP 解压和 SHA256 校验）
    std::string localModel = root + "/models/" + implFilename;
    std::string expectedSha = requestBody["implementation"].value("contentSha256", "");
    std::string finalModelPath;
    std::cout << "[PACKAGING] Downloading model from " << fileUri << " to " << localModel << std::endl;
    if (!downloadFile(fileUri, localModel, expectedSha, finalModelPath)) {
        res.status = 500;
        res.set_content(R"({"error":"Model download or integrity check failed"})", "application/json");
        return;
    }

    // 后续所有对模型路径的引用均使用 finalModelPath
    std::cout << "[PACKAGING] Model ready at: " << finalModelPath << std::endl;
    localModel = finalModelPath;

    // 2. 保存请求 JSON（格式化输出）
    std::string requestFilePath = root + "/requests/" + taskId + ".json";
    {
        std::ofstream ofs(requestFilePath);
        if (!ofs) {
            res.status = 500;
            res.set_content(R"({"error":"Failed to save request file"})", "application/json");
            return;
        }
        // 将 JSON 重新格式化：2 空格缩进
        std::string formatted = requestBody.dump(2);
        ofs << formatted;
        ofs.close();
    }
    std::cout << "[PACKAGING] Request saved to " << requestFilePath << std::endl;

    // 3. 调用 CLI，输出到 Upload_Result
    std::string outDir = root + "/" + std::string(DEFAULT_UPLOAD_DIR_NAME);
    std::cout << "[PACKAGING] Calling CLI...\n";
    if (!runPackagingCLI(requestFilePath, localModel, outDir,inputShape)) {
        res.status = 500;
        res.set_content(R"({"error":"Packaging failed"})", "application/json");
        return;
    }

    // 4. 检查 Packaging_Result 是否生成, 并收集运行时文件到临时打包目录
    std::string packagingResultDir = outDir + "/Packaging_Result";
    if (!std::filesystem::exists(packagingResultDir)) {
        res.status = 500;
        res.set_content(R"({"error":"Packaging_Result not found after CLI run"})", "application/json");
        return;
    }

    std::string packageDir = root + "/Upload_Result" + "/runtime_package";
    std::string zipName = blockName + "_packaging_result.zip";
    std::string zipPath = outDir + "/" + zipName;

    try {
        std::filesystem::remove_all(packageDir);  // 清理旧目录
        collectRuntimeFiles(outDir, implType, packageDir);
        std::filesystem::remove_all(packagingResultDir);
    }
    catch (const std::exception& e) {
        std::cerr << "[PACKAGING] Exception during packaging: " << e.what() << std::endl;
        res.status = 500;
        res.set_content(std::string("{\"error\":\"Unexpected error: ") + e.what() + "\"}", "application/json");
        std::filesystem::remove(zipPath);
        std::filesystem::remove_all(packageDir);
        std::filesystem::remove_all(packagingResultDir); // 清理可能残留的目录
        return;
    }

    // 5. 压缩
    std::cout << "[PACKAGING] Creating ZIP: " << zipPath << std::endl;
    try {
        zipFolder(packageDir, zipPath);
    }
    catch (const std::exception& e) {
        res.status = 500;
        res.set_content(std::string("{\"error\":\"ZIP creation failed: ") + e.what() + "\"}", "application/json");
        return;
    }
    std::filesystem::remove_all(packageDir);

    // 6. SHA256
    std::string sha256 = sha256File(zipPath);
    std::cout << "[PACKAGING] SHA256: " << sha256 << std::endl;

    // 7. 上传 MinIO（若失败则 fallback 到本地文件）
// NOTE: 先调用上传接口 http://10.95.210.240:8080/api/v1/files/upload
    std::string uploadedUri = uploadResultZipViaHttp(zipPath);
    if (uploadedUri.empty()) {
        json fallback;
        fallback["resultPackageUri"] = "file:///" + std::filesystem::absolute(zipPath).string();
        fallback["resultPackageSha256"] = sha256;
        fallback["storageType"] = "LOCAL";
        res.set_content(fallback.dump(), "application/json");
        std::cerr << "[Upload] upload failed, fallback to local storage.\n";
        return;
    }

    // 8. 上传成功后回调 ADP（把 taskId 原路返回，并携带 artifactUri=downloadUrl）
    // NOTE: 再调用回调接口 http://10.95.210.240:33000/internal/module2/callbacks/wrapper-codegen
    std::string cbErr;
    {
        json warnings = json::object(); // 你可以后续回传 warnings
        std::string wrapperClass = requestBody["generationOptions"].value("className", "");

        bool cbOk = callbackAdpWrapperCodegen(
            taskId,
            /*success*/ true,
            /*artifactUri*/ uploadedUri,
            /*artifactSha256*/ sha256,
            /*wrapperClass*/ wrapperClass,
            /*modelType*/ implType,
            /*runtime*/ "qt_cpp",
            /*fileCount*/ 0,
            warnings,
            cbErr
        );

        if (!cbOk) {
            // 回调失败不阻断：文件已经上传成功，仍返回 uploadedUri 方便后续人工/自动重试回调
            json response;
            response["resultPackageUri"] = uploadedUri;
            response["resultPackageSha256"] = sha256;
            response["storageType"] = "MINIO";
            response["status"] = "SUCCEEDED";
            response["message"] = std::string("Upload succeeded but ADP callback failed: ") + cbErr;
            res.set_content(response.dump(), "application/json");
            std::cerr << "[ADP] callback failed: " << cbErr << "\n";
            return;
        }
    }

    // 9. 成功响应
    json response;
    response["resultPackageUri"] = uploadedUri;              // upload 接口返回的 downloadUrl
    response["resultPackageSha256"] = sha256;
    response["storageType"] = "MINIO";
    response["status"] = "SUCCEEDED";
    res.set_content(response.dump(), "application/json");
    std::cout << "[PACKAGING] Uploaded to MinIO and callback sent.\n";

    // 所有中间文件均保留，不再删除
}


int main() {
#ifdef _WIN32
    // 让 Windows 控制台用 UTF-8 显示（影响 std::cout/cerr 输出）
    SetConsoleOutputCP(CP_UTF8);
#endif
    // 保持你的原注释和逻辑：将 CWD 固定到项目根目录，确保相对路径稳定
    std::error_code ec;
    // fs::current_path(fs::path(PROJECT_ROOT), ec);
    // if (ec) {
    //     std::cerr << "[ERROR] Failed to set current working directory to PROJECT_ROOT: " << ec.message() << std::endl;
    //     return 1;
    // }
    setWorkingDirectoryOrWarn();
    // 加载配置文件（可通过环境变量指定路径）
    const std::string configPath = getEnvOrEmpty("CONFIG_PATH").empty()
        ? std::string(PROJECT_ROOT) + "/config.json"
        : getEnvOrEmpty("CONFIG_PATH");
    g_config.loadFromFile(configPath);

    httplib::Server svr;

    // 原有路由：包装服务
    svr.Post("/api/v1/packaging", handlePackaging);

    // 新增路由：产物 ZIP 下载（供 ADP 通过 artifactUri 下载）
    svr.Get(R"(/internal/module2/artifacts/(.*))", handleDownloadArtifact);

    std::string listenAddr = std::string(g_config.host) + ":" + std::string(g_config.port);
    std::cout << "Microservice running on http://" << listenAddr << std::endl;

    int port = 0;
    try {
        port = parsePortOrDie(g_config.port);
    }
    catch (const std::exception& e) {
        std::cerr << "[BOOT] Invalid MICROSERVICE_PORT='" << g_config.port << "': " << e.what() << "\n";
        return 1;
    }

    // 注意：svr.listen(host, port) 在失败时会返回 false，你也可以根据返回值打印错误
    svr.listen(g_config.host, port);
    return 0;
}

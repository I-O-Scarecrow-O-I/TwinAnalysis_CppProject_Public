// src/microservice/ResultUploader.h
#pragma once
#include <string>

// 上传结果 ZIP 到 Upload API（/api/v1/files/upload）
// 成功：返回 downloadUrl（用于后续 artifactUri）
// 失败：返回空字符串（让后续 fallback 本地流程正常运行）
std::string uploadResultZipViaHttp(const std::string& localZipPath);

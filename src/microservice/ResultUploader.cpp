// src/microservice/ResultUploader.cpp
#include "ResultUploader.h"

#include <iostream>
#include <fstream>
#include <string>

#include "utils/json.hpp"

#ifndef TWIN_ENABLE_RESULT_UPLOAD
#define TWIN_ENABLE_RESULT_UPLOAD 0
#endif

#ifndef TWIN_ADP_UPLOAD_BASE_URL
#define TWIN_ADP_UPLOAD_BASE_URL ""
#endif

#ifndef TWIN_ADP_UPLOAD_PATH
#define TWIN_ADP_UPLOAD_PATH "/api/v1/files/upload"
#endif

using json = nlohmann::ordered_json;

#if TWIN_ENABLE_RESULT_UPLOAD
#include <curl/curl.h>
#endif

namespace {
    bool file_exists_nonempty(const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f.is_open()) return false;
        f.seekg(0, std::ios::end);
        return f.tellg() > 0;
    }

#if TWIN_ENABLE_RESULT_UPLOAD
    size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* s = reinterpret_cast<std::string*>(userdata);
        s->append(ptr, size * nmemb);
        return size * nmemb;
    }
#endif
} // namespace

std::string uploadResultZipViaHttp(const std::string& localZipPath) {
#if !TWIN_ENABLE_RESULT_UPLOAD
    (void)localZipPath;
    return "";
#else
    const std::string baseUrl = TWIN_ADP_UPLOAD_BASE_URL;
    if (baseUrl.empty()) {
        std::cerr << "[Upload] TWIN_ADP_UPLOAD_BASE_URL is empty, skipping upload.\n";
        return "";
    }

    if (!file_exists_nonempty(localZipPath)) {
        std::cerr << "[Upload] zip file missing/empty: " << localZipPath << "\n";
        return "";
    }

    const std::string url = baseUrl + std::string(TWIN_ADP_UPLOAD_PATH);
    std::cout << "[Upload] POST " << url << " (file=" << localZipPath << ")\n";

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Upload] curl_easy_init failed\n";
        return "";
    }

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, localZipPath.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

    CURLcode rc = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        std::cerr << "[Upload] curl_easy_perform failed: " << curl_easy_strerror(rc) << "\n";
        return "";
    }

    if (httpCode != 201 && httpCode != 200) {
        std::cerr << "[Upload] HTTP " << httpCode << " response:\n" << responseBody << "\n";
        return "";
    }

    try {
        json j = json::parse(responseBody);
        if (j.contains("downloadUrl") && j["downloadUrl"].is_string()) {
            std::string downloadUrl = j["downloadUrl"].get<std::string>();
            if (!downloadUrl.empty()) return downloadUrl;
        }
        std::cerr << "[Upload] Upload succeeded but response missing downloadUrl.\n" << responseBody << "\n";
        return "";
    }
    catch (const std::exception& e) {
        std::cerr << "[Upload] Failed to parse upload response JSON: " << e.what() << "\n"
            << "[Upload] Body:\n" << responseBody << "\n";
        return "";
    }
#endif
}

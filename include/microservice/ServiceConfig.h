#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include "utils/json.hpp"

struct ServiceConfig {
    std::string host = MICROSERVICE_HOST;
    std::string port = MICROSERVICE_PORT;
    std::string uploadBaseUrl = ADP_UPLOAD_BASE_URL;
    std::string uploadPath = ADP_UPLOAD_PATH;
    std::string callbackBaseUrl = ADP_CALLBACK_BASE_URL;
    std::string callbackPath = ADP_CALLBACK_PATH;

    void loadFromFile(const std::string& configPath) {
        std::ifstream f(configPath);
        if (!f.is_open()) {
            std::cerr << "[CONFIG] Cannot open config file: " << configPath
                << ", using defaults." << std::endl;
            return;
        }

        try {
            nlohmann::ordered_json j;
            f >> j;
            if (j.contains("microservice")) {
                auto& ms = j["microservice"];
                if (ms.contains("host")) host = ms["host"].get<std::string>();
                if (ms.contains("port")) port = ms["port"].get<std::string>();
            }
            if (j.contains("adp")) {
                auto& adp = j["adp"];
                if (adp.contains("upload_base_url")) uploadBaseUrl = adp["upload_base_url"].get<std::string>();
                if (adp.contains("upload_path")) uploadPath = adp["upload_path"].get<std::string>();
                if (adp.contains("callback_base_url")) callbackBaseUrl = adp["callback_base_url"].get<std::string>();
                if (adp.contains("callback_path")) callbackPath = adp["callback_path"].get<std::string>();
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[CONFIG] Parse error: " << e.what() << ", using defaults." << std::endl;
        }
    }
};

// 全局配置对象声明（在其他 .cpp 中定义）
extern ServiceConfig g_config;

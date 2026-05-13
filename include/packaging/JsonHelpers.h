#pragma once
#include "utils/json.hpp"
#include <vector>
#include <string>

namespace pkg {

    inline std::vector<int64_t> json_to_shape_i64(const nlohmann::ordered_json& j) {
        std::vector<int64_t> out;
        if (!j.is_array()) return out;
        for (auto& v : j) out.push_back(v.get<int64_t>());
        return out;
    }

    inline int rank_from_shape(const std::vector<int64_t>& shape) {
        return static_cast<int>(shape.size());
    }

    inline std::string now_iso8601_local() {
        // reuse simplest format used in ModelParser
        // 2026-03-11T19:58:53 (no timezone) - acceptable for now
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        tm = *std::localtime(&t);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return std::string(buf);
    }

} // namespace pkg
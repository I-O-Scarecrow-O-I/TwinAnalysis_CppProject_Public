#include "packaging/RequestLoader.h"
#include "utils/json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::ordered_json;

static std::vector<int> parse_index_vec(const json& j) {
    std::vector<int> out;
    if (j.is_null()) return out;
    if (!j.is_array()) return out;
    for (auto& v : j) out.push_back(v.get<int>());
    return out;
}

PackagingTaskRequestLite RequestLoader::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open request json: " + path);

    json j;
    f >> j;

    PackagingTaskRequestLite req;
    req.protocolVersion = j.value("protocolVersion", "1.0");
    req.taskId = j.value("taskId", "");

    req.blockKey = j["block"].value("blockKey", "");
    req.blockName = j["block"].value("blockName", "");

    req.implementationType = j["implementation"].value("type", "");
    req.implementationFilename = j["implementation"].value("filename", "");
    req.deliveryPackageRelativePath = j["implementation"].value("deliveryPackageRelativePath", "");

    // generationOptions
    if (j.contains("generationOptions")) {
        auto go = j["generationOptions"];
        req.generationOptions.language = go.value("language", "C++");
        req.generationOptions.cppStandard = go.value("cppStandard", "C++17");
        req.generationOptions.nameSpace = go.value("namespace", "twin.generated");
        req.generationOptions.className = go.value("className", "GeneratedWrapper");
        req.generationOptions.emitMappingTable = go.value("emitMappingTable", true);
        req.generationOptions.emitWarnings = go.value("emitWarnings", true);
        req.generationOptions.emitUnmatchedList = go.value("emitUnmatchedList", true);
    }

    // expectedBindings
    if (j.contains("expectedBindings") && j["expectedBindings"].is_array()) {
        for (auto& eb : j["expectedBindings"]) {
            ExpectedBinding b;
            b.sysmlPath = eb.value("sysmlPath", "");

            auto tgt = eb["target"];
            b.target.name = tgt.value("name", "");
            b.target.kind = tgt.value("kind", "");
            b.target.role = tgt.value("role", "");
            b.target.index = parse_index_vec(tgt["index"]);

            // Parse slice field (null or missing -> no slice)
            if (tgt.contains("slice") && !tgt["slice"].is_null()) {
                const auto& s = tgt["slice"];
                SliceSpec ss;
                ss.start  = s.value("start",  int64_t(0));
                ss.length = s.value("length", int64_t(0));
                b.target.slice = ss;
            }

            b.expectedDirection = eb.value("expectedDirection", "");
            b.expectedType = eb.value("expectedType", "");
            b.expectedPrecision = eb.value("expectedPrecision", "");

            if (eb.contains("expectedShape") && eb["expectedShape"].is_array()) {
                for (auto& d : eb["expectedShape"]) b.expectedShape.push_back(d.get<int64_t>());
            }
            req.expectedBindings.push_back(std::move(b));
        }
    }

    // portMapping
    if (j.contains("portMapping") && j["portMapping"].is_object()) {
        for (auto& [portName, fieldMap] : j["portMapping"].items()) {
            if (fieldMap.is_object()) {
                for (auto& [fieldName, tensorNameVal] : fieldMap.items()) {
                    if (tensorNameVal.is_string())
                        req.portMapping[portName][fieldName] = tensorNameVal.get<std::string>();
                }
            }
        }
    }

    return req;
}
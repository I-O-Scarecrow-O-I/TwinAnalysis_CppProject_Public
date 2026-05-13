#pragma once
#include "packaging/PackagingTypes.h"
#include "utils/json.hpp"
#include <string>

class PackagingResultWriter {
public:
    static void writeAll(const PackagingTaskRequestLite& req,
        const std::string& outPackagingResultDir,
        const nlohmann::ordered_json& resultMetadata,
        const nlohmann::ordered_json& detectedModelMeta,
        const nlohmann::ordered_json& mappingTable,
        const nlohmann::ordered_json& unmatchedItems,
        const nlohmann::ordered_json& warnings,
        const nlohmann::ordered_json& compatibilityChecks,
        const nlohmann::ordered_json& modelParserMeta,   // raw-extract: ONNX ModelParser output (may be null)
        const std::string& requestPath,                  // raw-extract: traceability
        const std::string& modelPath,                    // raw-extract/docs
        const std::string& frameworkParseLogText,        // framework_parse_log.txt content
        const nlohmann::ordered_json& frameworkIoDump    // raw-extract: tf_io_dump.json or torch_io_dump.json (may be null)
    );
};
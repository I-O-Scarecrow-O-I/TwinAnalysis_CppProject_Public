#pragma once
#include "packaging/PackagingTypes.h"
#include "utils/json.hpp"
#include <string>
#include <map>

struct BindingCheckOutputs {
    nlohmann::ordered_json mappingTable;          // array
    nlohmann::ordered_json unmatchedItems;        // array
    nlohmann::ordered_json warnings;              // array
    nlohmann::ordered_json compatibilityChecks;   // array
};

class BindingChecker {
public:
    // detectedMeta: Detected_Model_Meta.json ½á¹¹
    static BindingCheckOutputs runChecks(const PackagingTaskRequestLite& req,
        const nlohmann::ordered_json& detectedMeta);
};
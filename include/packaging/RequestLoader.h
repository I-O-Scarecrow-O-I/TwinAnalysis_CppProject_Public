#pragma once
#include "packaging/PackagingTypes.h"
#include <string>

class RequestLoader {
public:
    static PackagingTaskRequestLite loadFromFile(const std::string& path);
};
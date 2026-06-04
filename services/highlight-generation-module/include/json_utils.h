#pragma once
#include "models.h"
#include <string>
#include <vector>

class JsonUtils {
public:
    static std::string trim(const std::string& str);
    static std::string extractValue(const std::string& json, const std::string& key);
    static std::vector<VisionEvent> parseEvents(const std::string& filePath);
    static RecordIndex parseRecordIndex(const std::string& filePath);
    static std::map<std::string, ClipPolicy> parseClipPolicies(const std::string& jsonBody);
    static std::string makeStatusResponse(int code, const std::string& msg, const std::string& status, const std::string& path);
};
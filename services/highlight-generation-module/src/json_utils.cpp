#include "../include/json_utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>

std::string JsonUtils::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"");
    return str.substr(first, (last - first + 1));
}

std::string JsonUtils::extractValue(const std::string& json, const std::string& key) {
    size_t keyPos = json.find("\"" + key + "\"");
    if (keyPos == std::string::npos) return "";
    size_t colonPos = json.find(":", keyPos);
    if (colonPos == std::string::npos) return "";
    size_t commaPos = json.find(",", colonPos);
    size_t bracePos = json.find("}", colonPos);
    size_t endPos = (commaPos < bracePos) ? commaPos : bracePos;
    return trim(json.substr(colonPos + 1, endPos - colonPos - 1));
}

std::vector<VisionEvent> JsonUtils::parseEvents(const std::string& filePath) {
    std::vector<VisionEvent> events;
    std::ifstream file(filePath);
    if (!file.is_open()) return events;
    std::stringstream buf; buf << file.rdbuf(); file.close();
    std::string content = buf.str();
    size_t startPos = 0;
    while ((startPos = content.find("{", startPos)) != std::string::npos) {
        size_t endPos = content.find("}", startPos);
        if (endPos == std::string::npos) break;
        std::string obj = content.substr(startPos, endPos - startPos + 1);
        VisionEvent ev;
        ev.event_id = extractValue(obj, "event_id");
        ev.event_type = extractValue(obj, "event_type");
        std::string s_start = extractValue(obj, "start_sec");
        std::string s_end = extractValue(obj, "end_sec");
        std::string s_conf = extractValue(obj, "confidence");
        if (!s_start.empty()) ev.start_sec = std::stod(s_start);
        if (!s_end.empty()) ev.end_sec = std::stod(s_end);
        if (!s_conf.empty()) ev.confidence = std::stod(s_conf);
        ev.camera_id = extractValue(obj, "camera_id");
        if (!ev.event_id.empty()) events.push_back(ev);
        startPos = endPos + 1;
    }
    return events;
}

RecordIndex JsonUtils::parseRecordIndex(const std::string& filePath) {
    RecordIndex idx;
    std::ifstream file(filePath);
    if (!file.is_open()) return idx;
    std::stringstream buf; buf << file.rdbuf(); file.close();
    std::string c = buf.str();
    idx.match_id = extractValue(c, "match_id");
    std::string dur = extractValue(c, "duration_sec");
    if (!dur.empty()) idx.duration_sec = std::stod(dur);
    idx.cam_01_raw_path = extractValue(c, "cam_01_raw_path");
    idx.cam_02_raw_path = extractValue(c, "cam_02_raw_path");
    idx.program_path = extractValue(c, "program_path");
    idx.status = extractValue(c, "status");
    return idx;
}

std::map<std::string, ClipPolicy> JsonUtils::parseClipPolicies(const std::string& jsonBody) {
    std::map<std::string, ClipPolicy> policies;
    std::vector<std::string> types = {"goal_candidate", "shot_candidate", "danger_attack_candidate", "celebration_candidate"};
    for (const auto& t : types) {
        size_t typePos = jsonBody.find("\"" + t + "\"");
        if (typePos != std::string::npos) {
            size_t blockEnd = jsonBody.find("}", typePos);
            std::string sub = jsonBody.substr(typePos, blockEnd - typePos + 1);
            ClipPolicy p;
            std::string pre = extractValue(sub, "pre_sec");
            std::string post = extractValue(sub, "post_sec");
            if (!pre.empty()) p.pre_sec = std::stod(pre);
            if (!post.empty()) p.post_sec = std::stod(post);
            policies[t] = p;
        }
    }
    return policies;
}

std::string JsonUtils::makeStatusResponse(int code, const std::string& msg, const std::string& status, const std::string& path) {
    std::stringstream ss;
    ss << "{\n  \"code\": " << code << ",\n  \"message\": \"" << msg << "\",\n  \"data\": {\n"
       << "    \"status\": \"" << status << "\",\n    \"output_path\": \"" << path << "\"\n  }\n}";
    return ss.str();
}
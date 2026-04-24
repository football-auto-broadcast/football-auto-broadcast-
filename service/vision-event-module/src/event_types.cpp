/**
 * event_types.cpp - 事件类型实现
 *
 * 实现 EventType 枚举和 Event 结构体的序列化方法。
 */

#include "event_types.hpp"

#include <sstream>

namespace vision {

// ============================================================================
// EventType 枚举转换
// ============================================================================

const char* event_type_to_string(EventType type) {
    switch (type) {
        case EventType::GOAL_CANDIDATE:           return "goal_candidate";
        case EventType::SHOT_CANDIDATE:           return "shot_candidate";
        case EventType::DANGER_ATTACK_CANDIDATE:  return "danger_attack_candidate";
        case EventType::CELEBRATION_CANDIDATE:    return "celebration_candidate";
        default:                                  return "unknown";
    }
}

EventType event_type_from_string(const std::string& str) {
    if (str == "goal_candidate")           return EventType::GOAL_CANDIDATE;
    if (str == "shot_candidate")           return EventType::SHOT_CANDIDATE;
    if (str == "danger_attack_candidate")  return EventType::DANGER_ATTACK_CANDIDATE;
    if (str == "celebration_candidate")    return EventType::CELEBRATION_CANDIDATE;
    return static_cast<EventType>(-1);
}

// ============================================================================
// Event 结构体方法
// ============================================================================

bool Event::is_valid() const {
    if (event_id.empty() || event_id.substr(0, 4) != "evt_") return false;
    if (start_sec < 0.0) return false;
    if (end_sec <= start_sec) return false;
    if (confidence < 0.0 || confidence > 1.0) return false;
    if (camera_id != "cam_01" && camera_id != "cam_02") return false;
    return true;
}

double Event::duration_sec() const {
    return end_sec - start_sec;
}

std::string Event::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"event_id\":\"" << event_id << "\",";
    oss << "\"event_type\":\"" << event_type_to_string(event_type) << "\",";
    oss << "\"start_sec\":" << start_sec << ",";
    oss << "\"end_sec\":" << end_sec << ",";
    oss << "\"confidence\":" << confidence << ",";
    oss << "\"camera_id\":\"" << camera_id << "\"";
    oss << "}";
    return oss.str();
}

} // namespace vision

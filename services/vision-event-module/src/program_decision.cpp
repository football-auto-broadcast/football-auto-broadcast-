/**
 * program_decision.cpp - 节目决策实现
 *
 * 基于多机位分析结果生成最终导播切镜决策。
 */

#include "program_decision.hpp"

#include <sstream>

namespace vision {

// ============================================================================
// DecisionReason 枚举转换
// ============================================================================

const char* decision_reason_to_string(DecisionReason reason) {
    switch (reason) {
        case DecisionReason::GLOBAL_PLAY_TRACKING:       return "global_play_tracking";
        case DecisionReason::GOAL_AREA_ACTIVITY_BOOSTED: return "goal_area_activity_boosted";
        case DecisionReason::SIX_YARD_BOX_HIGHLIGHT:     return "six_yard_box_highlight";
        case DecisionReason::DEFAULT_MAIN_CAMERA:        return "default_main_camera";
        case DecisionReason::AUX_CAMERA_FALLBACK:        return "aux_camera_fallback";
        default:                                         return "unknown";
    }
}

DecisionReason decision_reason_from_string(const std::string& str) {
    if (str == "global_play_tracking")        return DecisionReason::GLOBAL_PLAY_TRACKING;
    if (str == "goal_area_activity_boosted")  return DecisionReason::GOAL_AREA_ACTIVITY_BOOSTED;
    if (str == "six_yard_box_highlight")      return DecisionReason::SIX_YARD_BOX_HIGHLIGHT;
    if (str == "default_main_camera")         return DecisionReason::DEFAULT_MAIN_CAMERA;
    if (str == "aux_camera_fallback")         return DecisionReason::AUX_CAMERA_FALLBACK;
    return DecisionReason::DEFAULT_MAIN_CAMERA;
}

// ============================================================================
// ProgramDecision 方法
// ============================================================================

bool ProgramDecision::is_valid() const {
    if (match_id.empty()) return false;
    if (timestamp_ms < 0) return false;
    if (recommended_camera_id != "cam_01" && recommended_camera_id != "cam_02") return false;
    if (confidence < 0.0 || confidence > 1.0) return false;
    return true;
}

std::string ProgramDecision::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"match_id\":\"" << match_id << "\",";
    oss << "\"timestamp_ms\":" << timestamp_ms << ",";
    oss << "\"recommended_camera_id\":\"" << recommended_camera_id << "\",";
    oss << "\"reason\":\"" << decision_reason_to_string(reason) << "\",";
    oss << "\"confidence\":" << confidence;
    oss << "}";
    return oss.str();
}

ProgramDecision ProgramDecision::from_json(const std::string& json) {
    // TODO: 实现 JSON 反序列化
    ProgramDecision decision;
    decision.recommended_camera_id = "cam_01";
    decision.reason = DecisionReason::DEFAULT_MAIN_CAMERA;
    return decision;
}

} // namespace vision

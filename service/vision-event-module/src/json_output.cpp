/**
 * json_output.cpp - JSON 输出实现
 *
 * 统一 API 响应格式和错误码定义。
 */

#include "json_output.hpp"

#include <sstream>

namespace vision {

// ============================================================================
// 错误码转换
// ============================================================================

const char* error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::OK:                    return "ok";
        case ErrorCode::ERR_PARAM:             return "参数错误";
        case ErrorCode::ERR_NOT_INITIALIZED:   return "资源未初始化";
        case ErrorCode::ERR_SOURCE_UNAVAILABLE: return "输入源不可用";
        case ErrorCode::ERR_STREAM_EXCEPTION:  return "视频流异常";
        case ErrorCode::ERR_SDK_INIT_FAILED:   return "工业相机 SDK 初始化失败";
        case ErrorCode::ERR_ENUM_FAILED:       return "工业相机枚举失败";
        case ErrorCode::ERR_STREAM_FAILED:     return "相机取流失败";
        default:                               return "未知错误";
    }
}

// ============================================================================
// ApiResponse 方法
// ============================================================================

ApiResponse ApiResponse::ok(const std::string& data) {
    ApiResponse resp;
    resp.code = 0;
    resp.message = "ok";
    resp.data = data;
    return resp;
}

ApiResponse ApiResponse::error(ErrorCode code, const std::string& message) {
    ApiResponse resp;
    resp.code = static_cast<int>(code);
    resp.message = message.empty() ? error_code_to_string(code) : message;
    resp.data = "{}";
    return resp;
}

std::string ApiResponse::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"code\":" << code << ",";
    oss << "\"message\":\"" << message << "\",";
    oss << "\"data\":" << data;
    oss << "}";
    return oss.str();
}

// ============================================================================
// ModuleState 转换
// ============================================================================

const char* module_state_to_string(ModuleState state) {
    switch (state) {
        case ModuleState::IDLE:           return "idle";
        case ModuleState::INITIALIZING:   return "initializing";
        case ModuleState::RUNNING:        return "running";
        case ModuleState::STOPPED:        return "stopped";
        case ModuleState::FAILED:         return "failed";
        default:                          return "unknown";
    }
}

// ============================================================================
// ModuleStatus 方法
// ============================================================================

std::string ModuleStatus::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"match_id\":\"" << match_id << "\",";
    oss << "\"status\":\"" << module_state_to_string(status) << "\",";
    oss << "\"camera_main_status\":\"" << camera_main_status << "\",";
    oss << "\"camera_aux_status\":\"" << camera_aux_status << "\",";
    oss << "\"fps_main\":" << fps_main << ",";
    oss << "\"fps_aux\":" << fps_aux << ",";
    oss << "\"events_detected\":" << events_detected << ",";
    oss << "\"focus_region_cam_01_ready\":" << (focus_region_cam_01_ready ? "true" : "false") << ",";
    oss << "\"focus_region_cam_02_ready\":" << (focus_region_cam_02_ready ? "true" : "false") << ",";
    oss << "\"last_program_decision_camera\":\"" << last_program_decision_camera << "\",";
    oss << "\"error_message\":\"" << error_message << "\"";
    oss << "}";
    return oss.str();
}

} // namespace vision

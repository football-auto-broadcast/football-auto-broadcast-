/**
 * focus_region.cpp - 关注区域实现
 *
 * 实现单路关注区域的生成、平滑和序列化。
 */

#include "focus_region.hpp"

#include <sstream>

namespace vision {

// ============================================================================
// FocusRegionSource 枚举转换
// ============================================================================

const char* focus_region_source_to_string(FocusRegionSource source) {
    switch (source) {
        case FocusRegionSource::BALL_DETECTION: return "ball_detection";
        case FocusRegionSource::MOTION_CLUSTER: return "motion_cluster";
        case FocusRegionSource::DEFAULT:        return "default";
        default:                                return "unknown";
    }
}

FocusRegionSource focus_region_source_from_string(const std::string& str) {
    if (str == "ball_detection") return FocusRegionSource::BALL_DETECTION;
    if (str == "motion_cluster") return FocusRegionSource::MOTION_CLUSTER;
    if (str == "default")        return FocusRegionSource::DEFAULT;
    return FocusRegionSource::DEFAULT;
}

// ============================================================================
// Rect 方法
// ============================================================================

bool Rect::is_valid() const {
    return x >= 0 && y >= 0 && width > 0 && height > 0;
}

int Rect::area() const {
    return width * height;
}

// ============================================================================
// FocusRegion 方法
// ============================================================================

bool FocusRegion::is_valid() const {
    if (camera_id != "cam_01" && camera_id != "cam_02") return false;
    if (!rect.is_valid()) return false;
    if (confidence < 0.0 || confidence > 1.0) return false;
    return true;
}

std::string FocusRegion::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"camera_id\":\"" << camera_id << "\",";
    oss << "\"focus_region\":{";
    oss << "\"x\":" << rect.x << ",";
    oss << "\"y\":" << rect.y << ",";
    oss << "\"width\":" << rect.width << ",";
    oss << "\"height\":" << rect.height;
    oss << "},";
    oss << "\"source_type\":\"" << focus_region_source_to_string(source_type) << "\",";
    oss << "\"confidence\":" << confidence;
    oss << "}";
    return oss.str();
}

FocusRegion FocusRegion::from_json(const std::string& json) {
    // TODO: 实现 JSON 反序列化
    // 当前返回默认值
    FocusRegion region;
    region.camera_id = "cam_01";
    region.source_type = FocusRegionSource::DEFAULT;
    region.confidence = 0.0;
    return region;
}

} // namespace vision

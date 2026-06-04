/**
 * frame_input.cpp - 输入帧基础方法
 */

#include "frame_input.hpp"

namespace vision {

const char* frame_format_to_string(FrameFormat format) {
    switch (format) {
        case FrameFormat::BGR8:    return "bgr8";
        case FrameFormat::RGB8:    return "rgb8";
        case FrameFormat::GRAY8:   return "gray8";
        case FrameFormat::NV12:    return "nv12";
        case FrameFormat::YUV420P: return "yuv420p";
        default:                   return "unknown";
    }
}

bool InputFrame::is_valid() const {
    if (camera_id != "cam_01" && camera_id != "cam_02") return false;
    if (match_id.empty()) return false;
    if (timestamp_ms < 0 || frame_index < 0) return false;
    if (width <= 0 || height <= 0 || fps <= 0) return false;
    return true;
}

int InputFrame::total_pixels() const {
    return width * height;
}

} // namespace vision

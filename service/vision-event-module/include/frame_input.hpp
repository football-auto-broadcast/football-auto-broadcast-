/**
 * frame_input.hpp - 输入帧格式定义
 *
 * 定义视觉分析模块的输入帧结构 InputFrame 和帧格式枚举 FrameFormat。
 * 本模块接收 cam_01 和 cam_02 两路视频输入，每帧携带完整的元数据。
 */

#ifndef VISION_EVENT_MODULE_FRAME_INPUT_HPP
#define VISION_EVENT_MODULE_FRAME_INPUT_HPP

#include <cstdint>
#include <string>

namespace vision {

/**
 * @brief 帧格式枚举
 *
 * 定义输入图像的数据格式，默认使用 BGR8（OpenCV 标准格式）。
 */
enum class FrameFormat : int {
    BGR8 = 0,       ///< 3-channel BGR, 8-bit per channel (OpenCV default)
    RGB8 = 1,       ///< 3-channel RGB, 8-bit per channel
    GRAY8 = 2,      ///< 1-channel grayscale, 8-bit
    NV12 = 3,       ///< YUV 4:2:0, NV12 format
    YUV420P = 4     ///< YUV 4:2:0, planar format
};

/**
 * @brief 将 FrameFormat 枚举转换为字符串
 */
const char* frame_format_to_string(FrameFormat format);

/**
 * @brief 输入帧结构
 *
 * 每一帧输入携带图像数据及完整元数据，用于后续分析和事件检测。
 */
struct InputFrame {
    void* image = nullptr;          ///< 图像数据指针 (cv::Mat data)
    FrameFormat format = FrameFormat::BGR8;  ///< 帧格式
    std::string camera_id;          ///< 相机 ID: "cam_01" 或 "cam_02"
    std::string match_id;           ///< 比赛 ID
    int64_t timestamp_ms = 0;       ///< 帧时间戳 (毫秒)
    int64_t frame_index = 0;        ///< 帧序号 (从 0 开始)
    int width = 0;                  ///< 帧宽度 (像素)
    int height = 0;                 ///< 帧高度 (像素)
    int fps = 25;                   ///< 帧率

    /**
     * @brief 检查帧数据是否有效
     */
    bool is_valid() const;

    /**
     * @brief 获取帧的总像素数
     */
    int total_pixels() const;
};

} // namespace vision

#endif // VISION_EVENT_MODULE_FRAME_INPUT_HPP

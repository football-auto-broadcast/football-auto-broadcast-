/**
 * ball_detector.hpp - 足球检测器接口
 *
 * 使用 ONNX 模型检测足球位置和置信度。
 * 为关注区域生成和事件检测提供基础信号。
 */

#ifndef VISION_EVENT_MODULE_BALL_DETECTOR_HPP
#define VISION_EVENT_MODULE_BALL_DETECTOR_HPP

#include <string>
#include <memory>
#include <vector>

namespace vision {
namespace inference {

/**
 * @brief 足球检测结果
 */
struct BallDetection {
    float x = 0.0f;         ///< 足球中心 X (像素)
    float y = 0.0f;         ///< 足球中心 Y (像素)
    float width = 0.0f;     ///< 足球检测框宽度
    float height = 0.0f;    ///< 足球检测框高度
    float confidence = 0.0f;///< 检测置信度 [0, 1]
    bool detected = false;  ///< 是否检测到足球
};

/**
 * @brief 足球检测器
 *
 * 加载 ONNX 足球检测模型，对输入帧执行推理。
 */
class BallDetector {
public:
    explicit BallDetector(const std::string& model_path);
    ~BallDetector();

    /**
     * @brief 初始化检测器（加载模型）
     */
    bool initialize();

    /**
     * @brief 对输入帧执行足球检测
     * @param image_data 图像数据指针 (BGR8 格式)
     * @param width 图像宽度
     * @param height 图像高度
     * @return 检测结果
     */
    BallDetection detect(void* image_data, int width, int height);

    /**
     * @brief 检查检测器是否已初始化
     */
    bool is_initialized() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inference
} // namespace vision

#endif // VISION_EVENT_MODULE_BALL_DETECTOR_HPP

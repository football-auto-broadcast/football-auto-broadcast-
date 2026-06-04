/**
 * box_activity_analyzer.hpp - 禁区活跃度分析器接口
 *
 * 分析禁区内的运动强度和危险程度。
 * 主要用于小禁区精彩瞬间候选增强和危险进攻检测。
 */

#ifndef VISION_EVENT_MODULE_BOX_ACTIVITY_ANALYZER_HPP
#define VISION_EVENT_MODULE_BOX_ACTIVITY_ANALYZER_HPP

#include <memory>
#include <string>

namespace vision {
namespace inference {

/**
 * @brief 禁区活动分析结果
 */
struct BoxActivity {
    double intensity = 0.0;        ///< 禁区运动强度 [0, 1]
    double crowd_density = 0.0;    ///< 禁区内人群聚集程度 [0, 1]
    bool high_intensity = false;   ///< 是否高强度运动
    bool is_six_yard_box_highlight = false; ///< 是否小禁区精彩瞬间
};

/**
 * @brief 禁区活跃度分析器
 *
 * 检测禁区内的危险进攻信号和小禁区精彩瞬间。
 */
class BoxActivityAnalyzer {
public:
    BoxActivityAnalyzer();
    ~BoxActivityAnalyzer();

    /**
     * @brief 初始化分析器
     */
    bool initialize();

    /**
     * @brief 分析禁区活动
     *
     * @param image_data 图像数据指针
     * @param width 图像宽度
     * @param height 图像高度
     * @param ball_x 足球中心 X (像素)
     * @param ball_y 足球中心 Y (像素)
     * @return 禁区活动分析结果
     */
    BoxActivity analyze(void* image_data, int width, int height,
                        float ball_x, float ball_y);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inference
} // namespace vision

#endif // VISION_EVENT_MODULE_BOX_ACTIVITY_ANALYZER_HPP

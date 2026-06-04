/**
 * motion_analyzer.hpp - 运动分析器接口
 *
 * 分析画面中的运动聚集区域和全局活跃度。
 * 为关注区域生成提供运动趋势信号。
 */

#ifndef VISION_EVENT_MODULE_MOTION_ANALYZER_HPP
#define VISION_EVENT_MODULE_MOTION_ANALYZER_HPP

#include "focus_region.hpp"

#include <memory>
#include <string>

namespace vision {
namespace inference {

/**
 * @brief 运动分析结果
 */
struct MotionAnalysis {
    Rect motion_cluster_rect;   ///< 运动聚集区域
    double global_activity = 0.0; ///< 全局活跃度 [0, 1]
    double attack_direction = 0.0;  ///< 进攻方向角度 (度)
    bool has_significant_motion = false; ///< 是否有显著运动
};

/**
 * @brief 运动分析器
 *
 * 使用背景减除和轮廓检测分析画面中的运动模式。
 */
class MotionAnalyzer {
public:
    MotionAnalyzer();
    ~MotionAnalyzer();

    /**
     * @brief 初始化分析器
     */
    bool initialize();

    /**
     * @brief 分析输入帧的运动
     * @param image_data 图像数据指针 (BGR8 格式)
     * @param width 图像宽度
     * @param height 图像高度
     * @return 运动分析结果
     */
    MotionAnalysis analyze(void* image_data, int width, int height);

    /**
     * @brief 重置分析器状态（用于比赛重新开始）
     */
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inference
} // namespace vision

#endif // VISION_EVENT_MODULE_MOTION_ANALYZER_HPP

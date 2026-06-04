/**
 * goal_assist_analyzer.hpp - 进球辅助分析器接口
 *
 * 分析球门区域活动和进球可能性。
 * 主要用于进球候选事件的辅助确认。
 */

#ifndef VISION_EVENT_MODULE_GOAL_ASSIST_ANALYZER_HPP
#define VISION_EVENT_MODULE_GOAL_ASSIST_ANALYZER_HPP

#include <memory>
#include <string>

namespace vision {
namespace inference {

/**
 * @brief 球门区域活动分析结果
 */
struct GoalActivity {
    double goal_line_ball_proximity = 0.0; ///< 球与球门线接近程度 [0, 1]
    double goal_area_activity = 0.0;       ///< 球门区域活跃度 [0, 1]
    bool ball_near_goal_line = false;      ///< 球是否接近球门线
    bool high_activity_in_goal_area = false;///< 球门区域是否有高活跃度
};

/**
 * @brief 进球辅助分析器
 *
 * 分析球门区域活动模式，判断进球可能性。
 */
class GoalAssistAnalyzer {
public:
    GoalAssistAnalyzer();
    ~GoalAssistAnalyzer();

    /**
     * @brief 初始化分析器
     */
    bool initialize();

    /**
     * @brief 分析球门区域活动
     *
     * @param image_data 图像数据指针
     * @param width 图像宽度
     * @param height 图像高度
     * @param ball_x 足球中心 X (像素)
     * @param ball_y 足球中心 Y (像素)
     * @param ball_confidence 足球检测置信度
     * @return 球门区域活动分析结果
     */
    GoalActivity analyze(void* image_data, int width, int height,
                         float ball_x, float ball_y, float ball_confidence);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inference
} // namespace vision

#endif // VISION_EVENT_MODULE_GOAL_ASSIST_ANALYZER_HPP

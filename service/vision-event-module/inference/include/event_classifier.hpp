/**
 * event_classifier.hpp - 事件分类器接口
 *
 * 综合多模态特征识别进球/射门/危险进攻/庆祝事件。
 * 融合球检测、运动分析、禁区活动等信号生成候选事件。
 */

#ifndef VISION_EVENT_MODULE_EVENT_CLASSIFIER_HPP
#define VISION_EVENT_MODULE_EVENT_CLASSIFIER_HPP

#include "event_types.hpp"
#include "ball_detector.hpp"
#include "motion_analyzer.hpp"
#include "goal_assist_analyzer.hpp"
#include "box_activity_analyzer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vision {
namespace inference {

/**
 * @brief 事件分类器配置
 */
struct EventClassifierConfig {
    double goal_confidence_threshold = 0.7;
    double shot_confidence_threshold = 0.6;
    double danger_attack_confidence_threshold = 0.5;
    double celebration_confidence_threshold = 0.6;
    bool use_aux_boost_for_goal = true;
    bool use_aux_boost_for_shot = true;
};

/**
 * @brief 事件分类器
 *
 * 综合以下信号生成候选事件：
 * - cam_01: 全局进攻趋势、球门区域活动变化
 * - cam_02: 门前近景活动增强、球门线附近信号、小禁区高强度动作
 */
class EventClassifier {
public:
    explicit EventClassifier(const EventClassifierConfig& config);
    ~EventClassifier();

    /**
     * @brief 初始化分类器
     */
    bool initialize();

    /**
     * @brief 基于多模态信号分类事件
     *
     * @param ball_detection_main 主机位球检测结果
     * @param ball_detection_aux 辅机位球检测结果
     * @param motion_main 主机位运动分析结果
     * @param motion_aux 辅机位运动分析结果
     * @param goal_activity_main 主机位球门区域活动
     * @param goal_activity_aux 辅机位球门区域活动
     * @param box_activity_aux 辅机位小禁区活动
     * @return 检测到的候选事件列表
     */
    std::vector<Event> classify(
        const BallDetection& ball_detection_main,
        const BallDetection& ball_detection_aux,
        const MotionAnalysis& motion_main,
        const MotionAnalysis& motion_aux,
        const GoalActivity& goal_activity_main,
        const GoalActivity& goal_activity_aux,
        const BoxActivity& box_activity_aux
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace inference
} // namespace vision

#endif // VISION_EVENT_MODULE_EVENT_CLASSIFIER_HPP

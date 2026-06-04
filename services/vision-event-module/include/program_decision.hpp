/**
 * program_decision.hpp - 多机位决策结构定义
 *
 * 定义 ProgramDecision 结构，输出最终导播切镜决策结果。
 * 用于指导主画面模块选择当前推荐机位。
 */

#ifndef VISION_EVENT_MODULE_PROGRAM_DECISION_HPP
#define VISION_EVENT_MODULE_PROGRAM_DECISION_HPP

#include <cstdint>
#include <string>

namespace vision {

/**
 * @brief 决策原因枚举（冻结）
 *
 * 当前版本仅支持以下 5 种推荐原因。
 */
enum class DecisionReason : int {
    GLOBAL_PLAY_TRACKING = 0,         ///< global_play_tracking - 适合全场主机位
    GOAL_AREA_ACTIVITY_BOOSTED = 1,   ///< goal_area_activity_boosted - 门前活动增强
    SIX_YARD_BOX_HIGHLIGHT = 2,       ///< six_yard_box_highlight - 小禁区精彩瞬间
    DEFAULT_MAIN_CAMERA = 3,          ///< default_main_camera - 默认主机位策略
    AUX_CAMERA_FALLBACK = 4           ///< aux_camera_fallback - 辅机位备用策略
};

/**
 * @brief 将 DecisionReason 枚举转换为字符串
 */
const char* decision_reason_to_string(DecisionReason reason);

/**
 * @brief 从字符串解析 DecisionReason
 */
DecisionReason decision_reason_from_string(const std::string& str);

/**
 * @brief 多机位决策结果
 *
 * 描述当前帧推荐使用的机位及原因。
 */
struct ProgramDecision {
    std::string match_id;             ///< 比赛 ID
    int64_t timestamp_ms = 0;         ///< 毫秒时间戳
    std::string recommended_camera_id;///< 推荐机位: "cam_01" 或 "cam_02"
    DecisionReason reason;            ///< 推荐原因
    double confidence = 0.0;          ///< 置信度 [0, 1]

    /**
     * @brief 检查决策结果是否有效
     */
    bool is_valid() const;

    /**
     * @brief 将决策结果序列化为 JSON 字符串
     */
    std::string to_json() const;

    /**
     * @brief 从 JSON 字符串反序列化
     */
    static ProgramDecision from_json(const std::string& json);
};

} // namespace vision

#endif // VISION_EVENT_MODULE_PROGRAM_DECISION_HPP

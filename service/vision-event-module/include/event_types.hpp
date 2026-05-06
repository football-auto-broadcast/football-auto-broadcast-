/**
 * event_types.hpp - 事件类型枚举与数据结构定义
 *
 * 定义视觉分析模块输出的候选事件类型（进球/射门/危险进攻/庆祝候选）
 * 及 Event 数据结构。
 *
 * 事件类型枚举当前冻结为 4 个值，不允许私自新增。
 */

#ifndef VISION_EVENT_MODULE_EVENT_TYPES_HPP
#define VISION_EVENT_MODULE_EVENT_TYPES_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace vision {

/**
 * @brief 事件类型枚举（冻结）
 *
 * 当前版本仅支持以下 4 种候选事件类型。
 * 若后续扩展，必须升级文档版本。
 */
enum class EventType : int {
    GOAL_CANDIDATE = 0,             ///< 进球候选
    SHOT_CANDIDATE = 1,             ///< 射门候选
    DANGER_ATTACK_CANDIDATE = 2,    ///< 危险进攻候选
    CELEBRATION_CANDIDATE = 3       ///< 庆祝候选
};

/**
 * @brief 将 EventType 枚举转换为字符串
 * @return 事件类型的字符串表示，如 "goal_candidate"
 */
const char* event_type_to_string(EventType type);

/**
 * @brief 将字符串转换为 EventType 枚举
 * @return 对应的事件类型，若无法识别返回 EventType(-1)
 */
EventType event_type_from_string(const std::string& str);

/**
 * @brief 候选事件数据结构
 *
 * 表示一个被检测到的候选事件，包含时间范围、置信度和来源机位。
 */
struct Event {
    std::string event_id;           ///< 事件唯一 ID，格式: evt_ + 至少4位数字
    EventType event_type;           ///< 事件类型
    double start_sec = 0.0;         ///< 相对比赛开始时间 (秒)，>= 0
    double end_sec = 0.0;           ///< 相对比赛结束时间 (秒)，> start_sec
    double confidence = 0.0;        ///< 置信度，[0, 1]
    std::string camera_id;          ///< 事件主导来源机位: "cam_01" 或 "cam_02"

    /**
     * @brief 检查事件数据是否有效
     */
    bool is_valid() const;

    /**
     * @brief 获取事件持续时间 (秒)
     */
    double duration_sec() const;

    /**
     * @brief 将事件序列化为 JSON 字符串
     */
    std::string to_json() const;
};

/**
 * @brief 候选事件集合（用于 API 响应）
 */
struct EventList {
    std::string match_id;           ///< 比赛 ID
    std::vector<Event> events;      ///< 候选事件列表
};

} // namespace vision

#endif // VISION_EVENT_MODULE_EVENT_TYPES_HPP

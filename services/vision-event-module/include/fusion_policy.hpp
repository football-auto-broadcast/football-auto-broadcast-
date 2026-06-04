/**
 * fusion_policy.hpp - 双机位融合策略定义
 *
 * 定义多相机数据融合的规则和权重配置。
 */

#ifndef VISION_EVENT_MODULE_FUSION_POLICY_HPP
#define VISION_EVENT_MODULE_FUSION_POLICY_HPP

#include <string>

namespace vision {

/**
 * @brief 双机位融合策略配置
 *
 * 控制双机位数据融合的行为，包括是否启用融合、
 * 各事件类型的辅机位增强开关等。
 */
struct FusionPolicy {
    bool enable_dual_camera_fusion = true;          ///< 是否启用双机位融合
    bool enable_dual_camera_focus_regions = true;   ///< 是否启用双机位关注区域
    bool enable_program_decision = true;            ///< 是否启用多机位决策
    bool goal_candidate_use_aux_boost = true;       ///< 进球候选是否使用辅机位增强
    bool shot_candidate_use_aux_boost = true;       ///< 射门候选是否使用辅机位增强
    bool six_yard_box_enhancement = true;           ///< 是否启用小禁区增强
    std::string aux_camera_role = "goal_line_extension";  ///< 辅机位角色

    /**
     * @brief 检查配置是否有效
     */
    bool is_valid() const;

    /**
     * @brief 将配置序列化为 JSON 字符串
     */
    std::string to_json() const;

    /**
     * @brief 从 YAML 字符串加载配置
     */
    static FusionPolicy from_yaml(const std::string& yaml_content);
};

} // namespace vision

#endif // VISION_EVENT_MODULE_FUSION_POLICY_HPP

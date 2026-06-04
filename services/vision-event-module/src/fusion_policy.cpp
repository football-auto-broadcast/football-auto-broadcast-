/**
 * fusion_policy.cpp - 融合策略实现
 *
 * 实现双机位数据融合的算法和权重配置。
 */

#include "fusion_policy.hpp"

#include <sstream>

namespace vision {

bool FusionPolicy::is_valid() const {
    // 基本校验：辅机位角色不能为空
    return !aux_camera_role.empty();
}

std::string FusionPolicy::to_json() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"enable_dual_camera_fusion\":" << (enable_dual_camera_fusion ? "true" : "false") << ",";
    oss << "\"enable_dual_camera_focus_regions\":" << (enable_dual_camera_focus_regions ? "true" : "false") << ",";
    oss << "\"enable_program_decision\":" << (enable_program_decision ? "true" : "false") << ",";
    oss << "\"goal_candidate_use_aux_boost\":" << (goal_candidate_use_aux_boost ? "true" : "false") << ",";
    oss << "\"shot_candidate_use_aux_boost\":" << (shot_candidate_use_aux_boost ? "true" : "false") << ",";
    oss << "\"six_yard_box_enhancement\":" << (six_yard_box_enhancement ? "true" : "false") << ",";
    oss << "\"aux_camera_role\":\"" << aux_camera_role << "\"";
    oss << "}";
    return oss.str();
}

FusionPolicy FusionPolicy::from_yaml(const std::string& yaml_content) {
    // TODO: 实现 YAML 解析
    // 当前返回默认配置
    FusionPolicy policy;
    (void)yaml_content;
    return policy;
}

} // namespace vision

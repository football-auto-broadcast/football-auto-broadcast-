/**
 * test_fusion_policy.cpp - 融合策略单元测试
 *
 * 测试双机位融合策略的正确性。
 */

#include "fusion_policy.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace vision;

// ============================================================================
// 测试用例
// ============================================================================

void test_fusion_policy_default() {
    FusionPolicy policy;
    assert(policy.enable_dual_camera_fusion == true);
    assert(policy.enable_dual_camera_focus_regions == true);
    assert(policy.enable_program_decision == true);
    assert(policy.goal_candidate_use_aux_boost == true);
    assert(policy.shot_candidate_use_aux_boost == true);
    assert(policy.six_yard_box_enhancement == true);
    assert(policy.aux_camera_role == "goal_line_extension");
    assert(policy.is_valid());

    std::cout << "[PASS] test_fusion_policy_default" << std::endl;
}

void test_fusion_policy_to_json() {
    FusionPolicy policy;
    std::string json = policy.to_json();
    assert(json.find("enable_dual_camera_fusion") != std::string::npos);
    assert(json.find("goal_line_extension") != std::string::npos);

    std::cout << "[PASS] test_fusion_policy_to_json" << std::endl;
}

void test_fusion_policy_invalid() {
    FusionPolicy policy;
    policy.aux_camera_role = "";
    assert(!policy.is_valid());

    std::cout << "[PASS] test_fusion_policy_invalid" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "=== Fusion Policy Unit Tests ===" << std::endl;

    test_fusion_policy_default();
    test_fusion_policy_to_json();
    test_fusion_policy_invalid();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}

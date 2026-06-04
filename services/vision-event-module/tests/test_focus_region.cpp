/**
 * test_focus_region.cpp - 关注区域单元测试
 *
 * 测试单路关注区域的生成、验证和序列化。
 */

#include "focus_region.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace vision;

// ============================================================================
// 测试用例
// ============================================================================

void test_rect_is_valid() {
    Rect valid = {100, 200, 500, 300};
    assert(valid.is_valid());

    Rect invalid_width = {100, 200, 0, 300};
    assert(!invalid_width.is_valid());

    Rect invalid_height = {100, 200, 500, -1};
    assert(!invalid_height.is_valid());

    std::cout << "[PASS] test_rect_is_valid" << std::endl;
}

void test_rect_area() {
    Rect r = {0, 0, 100, 200};
    assert(r.area() == 20000);
    std::cout << "[PASS] test_rect_area" << std::endl;
}

void test_focus_region_source_to_string() {
    assert(std::string(focus_region_source_to_string(FocusRegionSource::BALL_DETECTION)) == "ball_detection");
    assert(std::string(focus_region_source_to_string(FocusRegionSource::MOTION_CLUSTER)) == "motion_cluster");
    assert(std::string(focus_region_source_to_string(FocusRegionSource::DEFAULT)) == "default");
    std::cout << "[PASS] test_focus_region_source_to_string" << std::endl;
}

void test_focus_region_is_valid() {
    FocusRegion valid;
    valid.camera_id = "cam_01";
    valid.rect = {100, 200, 500, 300};
    valid.source_type = FocusRegionSource::BALL_DETECTION;
    valid.confidence = 0.87;
    assert(valid.is_valid());

    // 无效：camera_id 错误
    FocusRegion invalid1 = valid;
    invalid1.camera_id = "cam_03";
    assert(!invalid1.is_valid());

    // 无效：confidence 超出范围
    FocusRegion invalid2 = valid;
    invalid2.confidence = 1.5;
    assert(!invalid2.is_valid());

    std::cout << "[PASS] test_focus_region_is_valid" << std::endl;
}

void test_focus_region_to_json() {
    FocusRegion region;
    region.camera_id = "cam_01";
    region.rect = {1200, 650, 1400, 800};
    region.source_type = FocusRegionSource::MOTION_CLUSTER;
    region.confidence = 0.87;

    std::string json = region.to_json();
    assert(json.find("cam_01") != std::string::npos);
    assert(json.find("motion_cluster") != std::string::npos);
    assert(json.find("1200") != std::string::npos);

    std::cout << "[PASS] test_focus_region_to_json" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "=== Focus Region Unit Tests ===" << std::endl;

    test_rect_is_valid();
    test_rect_area();
    test_focus_region_source_to_string();
    test_focus_region_is_valid();
    test_focus_region_to_json();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}

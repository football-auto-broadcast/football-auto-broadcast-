/**
 * test_multi_focus_region.cpp - 多关注区域单元测试
 *
 * 测试双机位关注区域融合功能。
 * 冻结规则：regions 必须同时包含 cam_01 与 cam_02。
 */

#include "multi_focus_region.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace vision;

// ============================================================================
// 测试用例
// ============================================================================

void test_multi_focus_region_is_valid() {
    MultiFocusRegion valid;
    valid.match_id = "match_20260405_001";
    valid.timestamp_ms = 1712323200123;

    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    main_region.rect = {1200, 650, 1400, 800};
    main_region.source_type = FocusRegionSource::MOTION_CLUSTER;
    main_region.confidence = 0.87;

    FocusRegion aux_region;
    aux_region.camera_id = "cam_02";
    aux_region.rect = {320, 180, 900, 600};
    aux_region.source_type = FocusRegionSource::BALL_DETECTION;
    aux_region.confidence = 0.91;

    valid.regions.push_back(main_region);
    valid.regions.push_back(aux_region);

    assert(valid.is_valid());
    assert(valid.main_region() != nullptr);
    assert(valid.aux_region() != nullptr);
    assert(valid.get_region("cam_01") != nullptr);
    assert(valid.get_region("cam_02") != nullptr);
    assert(valid.get_region("cam_03") == nullptr);

    std::cout << "[PASS] test_multi_focus_region_is_valid" << std::endl;
}

void test_multi_focus_region_missing_camera() {
    MultiFocusRegion invalid;
    invalid.match_id = "match_20260405_001";

    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    main_region.rect = {1200, 650, 1400, 800};
    main_region.source_type = FocusRegionSource::MOTION_CLUSTER;
    main_region.confidence = 0.87;

    invalid.regions.push_back(main_region);
    // 缺少 cam_02

    assert(!invalid.is_valid());

    std::cout << "[PASS] test_multi_focus_region_missing_camera" << std::endl;
}

void test_multi_focus_region_to_json() {
    MultiFocusRegion mfr;
    mfr.match_id = "match_20260405_001";
    mfr.timestamp_ms = 1712323200123;

    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    main_region.rect = {1200, 650, 1400, 800};
    main_region.source_type = FocusRegionSource::MOTION_CLUSTER;
    main_region.confidence = 0.87;

    FocusRegion aux_region;
    aux_region.camera_id = "cam_02";
    aux_region.rect = {320, 180, 900, 600};
    aux_region.source_type = FocusRegionSource::BALL_DETECTION;
    aux_region.confidence = 0.91;

    mfr.regions.push_back(main_region);
    mfr.regions.push_back(aux_region);

    std::string json = mfr.to_json();
    assert(json.find("match_20260405_001") != std::string::npos);
    assert(json.find("cam_01") != std::string::npos);
    assert(json.find("cam_02") != std::string::npos);

    std::cout << "[PASS] test_multi_focus_region_to_json" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "=== Multi Focus Region Unit Tests ===" << std::endl;

    test_multi_focus_region_is_valid();
    test_multi_focus_region_missing_camera();
    test_multi_focus_region_to_json();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}

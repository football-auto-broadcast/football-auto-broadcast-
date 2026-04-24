/**
 * test_event_types.cpp - 事件类型单元测试
 *
 * 测试 EventType 枚举和 Event 结构体的功能。
 */

#include "event_types.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace vision;

// ============================================================================
// 测试用例
// ============================================================================

void test_event_type_to_string() {
    assert(std::string(event_type_to_string(EventType::GOAL_CANDIDATE)) == "goal_candidate");
    assert(std::string(event_type_to_string(EventType::SHOT_CANDIDATE)) == "shot_candidate");
    assert(std::string(event_type_to_string(EventType::DANGER_ATTACK_CANDIDATE)) == "danger_attack_candidate");
    assert(std::string(event_type_to_string(EventType::CELEBRATION_CANDIDATE)) == "celebration_candidate");
    std::cout << "[PASS] test_event_type_to_string" << std::endl;
}

void test_event_type_from_string() {
    assert(event_type_from_string("goal_candidate") == EventType::GOAL_CANDIDATE);
    assert(event_type_from_string("shot_candidate") == EventType::SHOT_CANDIDATE);
    assert(event_type_from_string("danger_attack_candidate") == EventType::DANGER_ATTACK_CANDIDATE);
    assert(event_type_from_string("celebration_candidate") == EventType::CELEBRATION_CANDIDATE);
    std::cout << "[PASS] test_event_type_from_string" << std::endl;
}

void test_event_is_valid() {
    Event valid_event;
    valid_event.event_id = "evt_0001";
    valid_event.event_type = EventType::GOAL_CANDIDATE;
    valid_event.start_sec = 312.4;
    valid_event.end_sec = 320.6;
    valid_event.confidence = 0.92;
    valid_event.camera_id = "cam_02";
    assert(valid_event.is_valid());

    // 无效事件：event_id 格式错误
    Event invalid_event1 = valid_event;
    invalid_event1.event_id = "invalid";
    assert(!invalid_event1.is_valid());

    // 无效事件：confidence 超出范围
    Event invalid_event2 = valid_event;
    invalid_event2.confidence = 1.5;
    assert(!invalid_event2.is_valid());

    // 无效事件：end_sec <= start_sec
    Event invalid_event3 = valid_event;
    invalid_event3.end_sec = 300.0;
    assert(!invalid_event3.is_valid());

    std::cout << "[PASS] test_event_is_valid" << std::endl;
}

void test_event_to_json() {
    Event event;
    event.event_id = "evt_0001";
    event.event_type = EventType::GOAL_CANDIDATE;
    event.start_sec = 312.4;
    event.end_sec = 320.6;
    event.confidence = 0.92;
    event.camera_id = "cam_02";

    std::string json = event.to_json();
    assert(json.find("evt_0001") != std::string::npos);
    assert(json.find("goal_candidate") != std::string::npos);
    assert(json.find("cam_02") != std::string::npos);

    std::cout << "[PASS] test_event_to_json" << std::endl;
}

void test_event_duration() {
    Event event;
    event.start_sec = 312.4;
    event.end_sec = 320.6;
    assert(event.duration_sec() == 8.2);
    std::cout << "[PASS] test_event_duration" << std::endl;
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "=== Event Types Unit Tests ===" << std::endl;

    test_event_type_to_string();
    test_event_type_from_string();
    test_event_is_valid();
    test_event_to_json();
    test_event_duration();

    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}

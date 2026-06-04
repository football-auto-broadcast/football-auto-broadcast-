/**
 * test_service_algorithm.cpp - 规则视觉算法链路测试
 */

#include "service.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace vision;

void feed_simulated_frames(VisionService& service,
                           const std::string& match_id,
                           int start_frame,
                           int end_frame) {
    for (int i = start_frame; i <= end_frame; i += 5) {
        InputFrame main_frame;
        main_frame.camera_id = "cam_01";
        main_frame.match_id = match_id;
        main_frame.timestamp_ms = 1712323200000LL + i * 40;
        main_frame.frame_index = i;
        main_frame.width = 1920;
        main_frame.height = 1080;
        main_frame.fps = 25;
        main_frame.format = FrameFormat::BGR8;
        service.process_frame(main_frame);

        InputFrame aux_frame = main_frame;
        aux_frame.camera_id = "cam_02";
        service.process_frame(aux_frame);
    }
}

void test_dual_focus_regions_are_generated() {
    VisionService service("../config/default_config.yaml");
    const std::string match_id = "match_algo_001";
    assert(service.initialize());
    assert(service.init_match(match_id));
    assert(service.start_match(match_id));

    feed_simulated_frames(service, match_id, 0, 140);

    MultiFocusRegion regions = service.generate_focus_regions(match_id);
    assert(regions.is_valid());
    assert(regions.main_region() != nullptr);
    assert(regions.aux_region() != nullptr);

    std::cout << "[PASS] test_dual_focus_regions_are_generated" << std::endl;
}

void test_program_decision_and_events() {
    VisionService service("../config/default_config.yaml");
    const std::string match_id = "match_algo_002";
    assert(service.initialize());
    assert(service.init_match(match_id));
    assert(service.start_match(match_id));

    feed_simulated_frames(service, match_id, 0, 1200);

    ProgramDecision decision = service.generate_program_decision(match_id);
    assert(decision.is_valid());

    EventList events = service.get_event_candidates(match_id);
    assert(!events.events.empty());
    for (const auto& event : events.events) {
        assert(event.is_valid());
    }

    std::cout << "[PASS] test_program_decision_and_events" << std::endl;
}

int main() {
    std::cout << "=== Service Algorithm Unit Tests ===" << std::endl;
    test_dual_focus_regions_are_generated();
    test_program_decision_and_events();
    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}

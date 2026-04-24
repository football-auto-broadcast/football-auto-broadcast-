/**
 * service.cpp - 视觉分析服务主逻辑
 *
 * 管理服务生命周期、协调各组件工作。
 * 包括：初始化、启动/停止分析、帧处理、事件检测、关注区域生成、多机位决策。
 */

#include "service.hpp"
#include "frame_input.hpp"
#include "event_types.hpp"
#include "focus_region.hpp"
#include "multi_focus_region.hpp"
#include "program_decision.hpp"
#include "fusion_policy.hpp"
#include "camera_role.hpp"
#include "json_output.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>

namespace vision {

// ============================================================================
// 服务状态
// ============================================================================

struct VisionService::Impl {
    std::string config_path;
    std::atomic<ModuleState> state{ModuleState::IDLE};
    std::atomic<bool> running{false};

    FusionPolicy fusion_policy;

    // 每场比赛的状态
    struct MatchState {
        std::string match_id;
        bool running = false;
        int events_detected = 0;
        bool focus_region_cam_01_ready = false;
        bool focus_region_cam_02_ready = false;
        std::string last_program_decision_camera;
        std::string error_message;
    };
    std::map<std::string, MatchState> matches;
    std::mutex matches_mutex;

    // HTTP 服务器端口
    int http_port = 8083;
    std::string http_host = "127.0.0.1";
};

// ============================================================================
// 公共接口实现
// ============================================================================

VisionService::VisionService(const std::string& config_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->config_path = config_path;
}

VisionService::~VisionService() = default;

bool VisionService::initialize() {
    std::cout << "[service] Initializing with config: " << impl_->config_path << std::endl;

    // TODO: 加载配置文件
    // TODO: 初始化 HTTP 服务器
    // TODO: 初始化推理引擎 (ONNX Runtime)
    // TODO: 初始化相机连接

    impl_->state.store(ModuleState::INITIALIZING);

    // TODO: 实际初始化逻辑
    std::cout << "[service] Initialization complete" << std::endl;
    impl_->state.store(ModuleState::IDLE);
    return true;
}

void VisionService::run() {
    impl_->running.store(true);
    impl_->state.store(ModuleState::RUNNING);
    std::cout << "[service] Service running on " << impl_->http_host << ":" << impl_->http_port << std::endl;

    // 主循环：等待停止信号
    while (impl_->running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void VisionService::stop() {
    impl_->running.store(false);
    impl_->state.store(ModuleState::STOPPED);
    std::cout << "[service] Service stopping..." << std::endl;
}

ModuleState VisionService::state() const {
    return impl_->state.load();
}

// ============================================================================
// 比赛管理
// ============================================================================

bool VisionService::init_match(const std::string& match_id) {
    std::lock_guard<std::mutex> lock(impl_->matches_mutex);
    impl_->matches[match_id] = Impl::MatchState{.match_id = match_id};
    std::cout << "[service] Match initialized: " << match_id << std::endl;
    return true;
}

bool VisionService::start_match(const std::string& match_id) {
    std::lock_guard<std::mutex> lock(impl_->matches_mutex);
    auto it = impl_->matches.find(match_id);
    if (it == impl_->matches.end()) {
        return false;
    }
    it->second.running = true;
    std::cout << "[service] Match started: " << match_id << std::endl;
    return true;
}

bool VisionService::stop_match(const std::string& match_id) {
    std::lock_guard<std::mutex> lock(impl_->matches_mutex);
    auto it = impl_->matches.find(match_id);
    if (it == impl_->matches.end()) {
        return false;
    }
    it->second.running = false;
    std::cout << "[service] Match stopped: " << match_id << std::endl;
    return true;
}

// ============================================================================
// 帧处理骨架
// ============================================================================

void VisionService::process_frame(const InputFrame& frame) {
    // TODO: 实现帧处理流水线
    // 1. 球检测 (ball_detector)
    // 2. 运动分析 (motion_analyzer)
    // 3. 禁区活动分析 (box_activity_analyzer)
    // 4. 进球辅助分析 (goal_assist_analyzer)
    // 5. 事件分类 (event_classifier)
    // 6. 生成关注区域
    // 7. 生成多机位决策
    // 8. 推送结果

    (void)frame;
}

// ============================================================================
// 关注区域生成
// ============================================================================

MultiFocusRegion VisionService::generate_focus_regions(const std::string& match_id) {
    MultiFocusRegion result;
    result.match_id = match_id;
    result.timestamp_ms = 0; // TODO: 获取当前时间戳

    // TODO: 基于球检测、运动聚集等信号生成关注区域
    // cam_01 关注区域
    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    main_region.rect = {1200, 650, 1400, 800}; // 示例默认值
    main_region.source_type = FocusRegionSource::MOTION_CLUSTER;
    main_region.confidence = 0.87;

    // cam_02 关注区域
    FocusRegion aux_region;
    aux_region.camera_id = "cam_02";
    aux_region.rect = {320, 180, 900, 600}; // 示例默认值
    aux_region.source_type = FocusRegionSource::BALL_DETECTION;
    aux_region.confidence = 0.91;

    result.regions.push_back(main_region);
    result.regions.push_back(aux_region);

    return result;
}

// ============================================================================
// 多机位决策
// ============================================================================

ProgramDecision VisionService::generate_program_decision(const std::string& match_id) {
    ProgramDecision decision;
    decision.match_id = match_id;
    decision.timestamp_ms = 0; // TODO: 获取当前时间戳
    decision.recommended_camera_id = "cam_01";
    decision.reason = DecisionReason::DEFAULT_MAIN_CAMERA;
    decision.confidence = 0.85;

    // TODO: 基于当前场景分析生成决策
    // - 全场推进 → cam_01
    // - 门前活动增强 → cam_02
    // - 小禁区高活跃 → cam_02
    // - 不稳定 → cam_01

    return decision;
}

// ============================================================================
// 事件检测
// ============================================================================

EventList VisionService::get_event_candidates(const std::string& match_id) {
    EventList result;
    result.match_id = match_id;

    std::lock_guard<std::mutex> lock(impl_->matches_mutex);
    auto it = impl_->matches.find(match_id);
    if (it != impl_->matches.end()) {
        // TODO: 返回实际检测到的事件
    }

    return result;
}

} // namespace vision

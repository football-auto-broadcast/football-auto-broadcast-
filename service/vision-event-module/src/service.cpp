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
#include "http_server.hpp"

#include <iostream>
#include <atomic>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

namespace vision {

namespace {

constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;
constexpr double kPi = 3.14159265358979323846;

int64_t now_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

double clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

int clamp_int(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

Rect make_centered_16_9_rect(double center_x,
                             double center_y,
                             int frame_width,
                             int frame_height,
                             double scale) {
    frame_width = frame_width > 0 ? frame_width : kDefaultWidth;
    frame_height = frame_height > 0 ? frame_height : kDefaultHeight;

    const double safe_scale = std::max(0.20, std::min(1.0, scale));
    int width = static_cast<int>(frame_width * safe_scale);
    int height = static_cast<int>(width * 9.0 / 16.0);
    if (height > frame_height) {
        height = static_cast<int>(frame_height * safe_scale);
        width = static_cast<int>(height * 16.0 / 9.0);
    }

    width = clamp_int(width, 1, frame_width);
    height = clamp_int(height, 1, frame_height);

    int x = static_cast<int>(std::round(center_x - width / 2.0));
    int y = static_cast<int>(std::round(center_y - height / 2.0));
    x = clamp_int(x, 0, std::max(0, frame_width - width));
    y = clamp_int(y, 0, std::max(0, frame_height - height));
    return Rect{x, y, width, height};
}

std::string make_event_id(int event_counter) {
    std::ostringstream oss;
    oss << "evt_" << std::setw(4) << std::setfill('0') << event_counter;
    return oss.str();
}

void sleep_ms(int milliseconds) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(milliseconds));
#else
    (void)milliseconds;
#endif
}

std::string directory_name(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

bool is_absolute_path(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') return true;
    if (!path.empty() && (path[0] == '\\' || path[0] == '/')) return true;
    return false;
}

std::string join_path(const std::string& base, const std::string& child) {
    if (base.empty() || is_absolute_path(child)) return child;
    const char last = base[base.size() - 1];
    if (last == '\\' || last == '/') return base + child;
    return base + "/" + child;
}

void create_directory_recursive(const std::string& path) {
    if (path.empty()) return;
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        const char ch = path[i];
        current.push_back(ch);
        if (ch != '\\' && ch != '/') continue;
        if (current.size() > 1) {
#ifdef _WIN32
            _mkdir(current.c_str());
#endif
        }
    }
#ifdef _WIN32
    _mkdir(path.c_str());
#endif
}

std::string json_escape(const std::string& text) {
    std::ostringstream oss;
    for (char ch : text) {
        if (ch == '\\') oss << "\\\\";
        else if (ch == '"') oss << "\\\"";
        else if (ch == '\n') oss << "\\n";
        else if (ch == '\r') oss << "\\r";
        else oss << ch;
    }
    return oss.str();
}

} // namespace

// ============================================================================
// 服务状态
// ============================================================================

struct VisionService::Impl {
    std::string config_path;
    std::atomic<ModuleState> state{ModuleState::IDLE};
    std::atomic<bool> running{false};

    FusionPolicy fusion_policy;
    HttpServer* http_server = nullptr;
    std::string default_match_id = "match_20260405_001";
    std::string metadata_root = "../data/metadata";
    int input_width = kDefaultWidth;
    int input_height = kDefaultHeight;
    int input_fps = 25;
    int64_t simulation_frame_index = 0;
    int64_t last_simulation_tick_ms = 0;

    struct FrameSignal {
        std::string camera_id;
        int64_t timestamp_ms = 0;
        int64_t frame_index = 0;
        int width = kDefaultWidth;
        int height = kDefaultHeight;
        double center_x = kDefaultWidth / 2.0;
        double center_y = kDefaultHeight / 2.0;
        double activity = 0.0;
        double goal_activity = 0.0;
        double box_activity = 0.0;
        double ball_confidence = 0.0;
        bool valid = false;
    };

    // 每场比赛的状态
    struct MatchState {
        std::string match_id;
        bool running = false;
        int64_t match_start_timestamp_ms = 0;
        int events_detected = 0;
        int event_counter = 0;
        bool focus_region_cam_01_ready = false;
        bool focus_region_cam_02_ready = false;
        std::string last_program_decision_camera;
        std::string error_message;
        std::map<std::string, FrameSignal> latest_signals;
        std::vector<Event> events;
        double last_goal_event_sec = -1000.0;
        double last_shot_event_sec = -1000.0;
        double last_danger_event_sec = -1000.0;
        double last_celebration_event_sec = -1000.0;
    };
    std::map<std::string, MatchState> matches;

    // HTTP 服务器端口
    int http_port = 8083;
    std::string http_host = "127.0.0.1";

    ~Impl() {
        delete http_server;
    }

    static std::string trim(const std::string& value) {
        const size_t begin = value.find_first_not_of(" \t\r\n\"");
        if (begin == std::string::npos) return "";
        const size_t end = value.find_last_not_of(" \t\r\n\"");
        return value.substr(begin, end - begin + 1);
    }

    static std::string value_after_colon(const std::string& line) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) return "";
        std::string value = trim(line.substr(colon + 1));
        if (!value.empty() && value.front() == '"') value.erase(value.begin());
        if (!value.empty() && value.back() == '"') value.pop_back();
        return value;
    }

    bool load_config() {
        std::ifstream input(config_path);
        if (!input) {
            std::cerr << "[service] Config not found, using defaults: " << config_path << std::endl;
            return true;
        }

        std::string line;
        std::string section;
        std::string subsection;
        while (std::getline(input, line)) {
            const std::string stripped = trim(line);
            if (stripped.empty() || stripped[0] == '#') continue;

            const bool top_level = !line.empty() && line[0] != ' ' && line[0] != '\t';
            if (top_level && stripped.back() == ':') {
                section = stripped.substr(0, stripped.size() - 1);
                subsection.clear();
                continue;
            }
            if (!top_level && line.size() >= 3 && line[2] != ' ' && stripped.back() == ':') {
                subsection = stripped.substr(0, stripped.size() - 1);
                continue;
            }

            if (stripped.find("match_id:") == 0) {
                default_match_id = value_after_colon(stripped);
            } else if (section == "http" && stripped.find("port:") == 0) {
                http_port = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "http" && stripped.find("host:") == 0) {
                http_host = value_after_colon(stripped);
            } else if (section == "output" && stripped.find("metadata_root:") == 0) {
                metadata_root = value_after_colon(stripped);
            } else if (section == "input" && stripped.find("width:") == 0 && input_width <= 0) {
                input_width = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "input" && stripped.find("height:") == 0 && input_height <= 0) {
                input_height = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "input" && stripped.find("fps:") == 0) {
                input_fps = std::atoi(value_after_colon(stripped).c_str());
            }
        }

        if (http_port <= 0) http_port = 8083;
        if (http_host.empty()) http_host = "127.0.0.1";
        if (default_match_id.empty()) default_match_id = "match_20260405_001";
        if (metadata_root.empty()) metadata_root = "../data/metadata";
        if (!is_absolute_path(metadata_root)) {
            metadata_root = join_path(directory_name(config_path), metadata_root);
        }
        if (input_width <= 0) input_width = kDefaultWidth;
        if (input_height <= 0) input_height = kDefaultHeight;
        if (input_fps <= 0) input_fps = 25;
        return true;
    }

    std::string event_candidates_path(const std::string& match_id) const {
        return metadata_root + "/" + match_id + "/event_candidates.json";
    }

    FrameSignal estimate_signal_from_frame(const InputFrame& frame) const {
        FrameSignal signal;
        signal.camera_id = frame.camera_id;
        signal.timestamp_ms = frame.timestamp_ms > 0 ? frame.timestamp_ms : now_ms();
        signal.frame_index = frame.frame_index;
        signal.width = frame.width > 0 ? frame.width : kDefaultWidth;
        signal.height = frame.height > 0 ? frame.height : kDefaultHeight;

        if (frame.image && (frame.format == FrameFormat::BGR8 || frame.format == FrameFormat::RGB8)) {
            const unsigned char* pixels = static_cast<const unsigned char*>(frame.image);
            const int step_x = std::max(1, signal.width / 96);
            const int step_y = std::max(1, signal.height / 54);
            double weight_sum = 0.0;
            double weighted_x = 0.0;
            double weighted_y = 0.0;
            int active_samples = 0;
            int total_samples = 0;

            for (int y = 0; y < signal.height; y += step_y) {
                for (int x = 0; x < signal.width; x += step_x) {
                    const int idx = (y * signal.width + x) * 3;
                    const double b = pixels[idx + 0];
                    const double g = pixels[idx + 1];
                    const double r = pixels[idx + 2];
                    const double brightness = (r + g + b) / (3.0 * 255.0);
                    const double chroma = std::max({std::abs(r - g), std::abs(g - b), std::abs(r - b)}) / 255.0;
                    const double weight = std::max(0.0, brightness - 0.18) + chroma * 0.35;
                    if (weight > 0.18) {
                        weighted_x += x * weight;
                        weighted_y += y * weight;
                        weight_sum += weight;
                        ++active_samples;
                    }
                    ++total_samples;
                }
            }

            if (weight_sum > 0.0) {
                signal.center_x = weighted_x / weight_sum;
                signal.center_y = weighted_y / weight_sum;
                signal.activity = clamp01(static_cast<double>(active_samples) /
                                          std::max(1, total_samples) * 4.0);
                signal.ball_confidence = clamp01(weight_sum / std::max(1, total_samples));
            }
        } else if (frame.image && frame.format == FrameFormat::GRAY8) {
            const unsigned char* pixels = static_cast<const unsigned char*>(frame.image);
            const int step_x = std::max(1, signal.width / 96);
            const int step_y = std::max(1, signal.height / 54);
            double weight_sum = 0.0;
            double weighted_x = 0.0;
            double weighted_y = 0.0;
            int active_samples = 0;
            int total_samples = 0;

            for (int y = 0; y < signal.height; y += step_y) {
                for (int x = 0; x < signal.width; x += step_x) {
                    const double brightness = pixels[y * signal.width + x] / 255.0;
                    const double weight = std::max(0.0, brightness - 0.20);
                    if (weight > 0.12) {
                        weighted_x += x * weight;
                        weighted_y += y * weight;
                        weight_sum += weight;
                        ++active_samples;
                    }
                    ++total_samples;
                }
            }

            if (weight_sum > 0.0) {
                signal.center_x = weighted_x / weight_sum;
                signal.center_y = weighted_y / weight_sum;
                signal.activity = clamp01(static_cast<double>(active_samples) /
                                          std::max(1, total_samples) * 4.0);
                signal.ball_confidence = clamp01(weight_sum / std::max(1, total_samples));
            }
        } else {
            const double t = frame.fps > 0
                ? static_cast<double>(frame.frame_index) / frame.fps
                : static_cast<double>(frame.frame_index) / 25.0;
            const double phase = std::fmod(std::max(0.0, t), 48.0) / 48.0;
            const double wave = std::sin(2.0 * kPi * phase);
            const double attack_boost = phase > 0.68 ? (phase - 0.68) / 0.32 : 0.0;

            if (frame.camera_id == "cam_02") {
                signal.center_x = signal.width * (0.58 + 0.18 * wave);
                signal.center_y = signal.height * (0.48 + 0.08 * std::sin(4.0 * kPi * phase));
                signal.activity = clamp01(0.28 + 0.18 * std::abs(wave) + 0.54 * attack_boost);
                signal.ball_confidence = clamp01(0.36 + 0.42 * attack_boost);
            } else {
                signal.center_x = signal.width * (0.18 + 0.70 * phase);
                signal.center_y = signal.height * (0.50 + 0.12 * wave);
                signal.activity = clamp01(0.24 + 0.22 * std::abs(wave) + 0.34 * attack_boost);
                signal.ball_confidence = clamp01(0.30 + 0.30 * std::abs(wave));
            }
        }

        const bool right_goal_side = signal.center_x > signal.width * 0.72;
        const bool aux_goal_view = frame.camera_id == "cam_02";
        signal.goal_activity = clamp01((right_goal_side ? 0.45 : 0.0) +
                                       (aux_goal_view ? 0.18 : 0.0) +
                                       signal.activity * 0.55 +
                                       signal.ball_confidence * 0.35);
        signal.box_activity = clamp01((aux_goal_view ? 0.22 : 0.0) +
                                      signal.activity * 0.70 +
                                      signal.goal_activity * 0.35);
        signal.valid = true;
        return signal;
    }

    FrameSignal default_signal(const std::string& camera_id) const {
        FrameSignal signal;
        signal.camera_id = camera_id;
        signal.timestamp_ms = now_ms();
        signal.width = kDefaultWidth;
        signal.height = kDefaultHeight;
        signal.center_x = camera_id == "cam_02" ? kDefaultWidth * 0.58 : kDefaultWidth * 0.50;
        signal.center_y = kDefaultHeight * 0.50;
        signal.activity = 0.15;
        signal.goal_activity = camera_id == "cam_02" ? 0.28 : 0.12;
        signal.box_activity = camera_id == "cam_02" ? 0.30 : 0.10;
        signal.ball_confidence = 0.0;
        signal.valid = true;
        return signal;
    }

    FrameSignal latest_or_default(const MatchState& match, const std::string& camera_id) const {
        const auto it = match.latest_signals.find(camera_id);
        if (it != match.latest_signals.end() && it->second.valid) {
            return it->second;
        }
        return default_signal(camera_id);
    }

    void maybe_add_events(MatchState& match) {
        const FrameSignal main = latest_or_default(match, "cam_01");
        const FrameSignal aux = latest_or_default(match, "cam_02");
        const int64_t timestamp_ms = std::max(main.timestamp_ms, aux.timestamp_ms);
        if (match.match_start_timestamp_ms <= 0) {
            match.match_start_timestamp_ms = timestamp_ms;
        }
        const double event_sec =
            std::max(0.0, (timestamp_ms - match.match_start_timestamp_ms) / 1000.0);

        const double goal_score = clamp01(main.goal_activity * 0.45 + aux.goal_activity * 0.55);
        const double shot_score = clamp01(main.activity * 0.35 + aux.box_activity * 0.45 +
                                          aux.ball_confidence * 0.20);
        const double danger_score = clamp01(main.goal_activity * 0.50 + main.activity * 0.30 +
                                            aux.goal_activity * 0.20);
        const double celebration_score = clamp01(main.activity * 0.55 + aux.activity * 0.25 +
                                                 (goal_score > 0.78 ? 0.20 : 0.0));

        auto append_event = [&](EventType type, double start_sec, double end_sec,
                                double confidence, const std::string& camera_id) {
            Event event;
            event.event_id = make_event_id(++match.event_counter);
            event.event_type = type;
            event.start_sec = std::max(0.0, start_sec);
            event.end_sec = std::max(event.start_sec + 0.1, end_sec);
            event.confidence = clamp01(confidence);
            event.camera_id = camera_id;
            if (event.is_valid()) {
                match.events.push_back(event);
                match.events_detected = static_cast<int>(match.events.size());
            }
        };

        if (goal_score >= 0.78 && event_sec - match.last_goal_event_sec >= 10.0) {
            append_event(EventType::GOAL_CANDIDATE, event_sec - 4.0, event_sec + 5.0,
                         goal_score, aux.goal_activity >= main.goal_activity ? "cam_02" : "cam_01");
            match.last_goal_event_sec = event_sec;
        }
        if (shot_score >= 0.68 && event_sec - match.last_shot_event_sec >= 6.0) {
            append_event(EventType::SHOT_CANDIDATE, event_sec - 2.5, event_sec + 3.0,
                         shot_score, aux.box_activity >= main.activity ? "cam_02" : "cam_01");
            match.last_shot_event_sec = event_sec;
        }
        if (danger_score >= 0.62 && event_sec - match.last_danger_event_sec >= 8.0) {
            append_event(EventType::DANGER_ATTACK_CANDIDATE, event_sec - 5.0, event_sec + 5.0,
                         danger_score, "cam_01");
            match.last_danger_event_sec = event_sec;
        }
        if (celebration_score >= 0.74 && goal_score >= 0.70 &&
            event_sec - match.last_celebration_event_sec >= 12.0) {
            append_event(EventType::CELEBRATION_CANDIDATE, event_sec, event_sec + 12.0,
                         celebration_score, "cam_01");
            match.last_celebration_event_sec = event_sec;
        }
    }
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

    impl_->state.store(ModuleState::INITIALIZING);

    if (!impl_->load_config()) {
        impl_->state.store(ModuleState::FAILED);
        return false;
    }

    delete impl_->http_server;
    impl_->http_server = new HttpServer(impl_->http_host, impl_->http_port, this);
    if (!impl_->http_server->start()) {
        impl_->state.store(ModuleState::FAILED);
        return false;
    }

    std::cout << "[service] Initialization complete" << std::endl;
    impl_->state.store(ModuleState::IDLE);
    return true;
}

void VisionService::run() {
    impl_->running.store(true);
    impl_->state.store(ModuleState::RUNNING);
    std::cout << "[service] Service running on " << impl_->http_host << ":" << impl_->http_port << std::endl;

    impl_->last_simulation_tick_ms = now_ms();
    while (impl_->running.load()) {
        if (impl_->http_server) {
            impl_->http_server->poll_once(20);
        } else {
            sleep_ms(20);
        }
        process_simulated_frames();
    }
}

void VisionService::stop() {
    impl_->running.store(false);
    if (impl_->http_server) {
        impl_->http_server->stop();
    }
    impl_->state.store(ModuleState::STOPPED);
    std::cout << "[service] Service stopping..." << std::endl;
}

ModuleState VisionService::state() const {
    return impl_->state.load();
}

ModuleStatus VisionService::get_status(const std::string& match_id) const {
    ModuleStatus status;
    status.match_id = match_id;
    status.status = impl_->state.load();
    status.camera_main_status = "offline";
    status.camera_aux_status = "offline";
    status.fps_main = 0;
    status.fps_aux = 0;
    status.events_detected = 0;
    status.focus_region_cam_01_ready = false;
    status.focus_region_cam_02_ready = false;
    status.last_program_decision_camera = "cam_01";

    const auto it = impl_->matches.find(match_id);
    if (it != impl_->matches.end()) {
        const auto& match = it->second;
        status.status = match.running ? ModuleState::RUNNING : impl_->state.load();
        status.camera_main_status = match.latest_signals.count("cam_01") ? "online" : "offline";
        status.camera_aux_status = match.latest_signals.count("cam_02") ? "online" : "offline";
        status.fps_main = match.latest_signals.count("cam_01") ? impl_->input_fps : 0;
        status.fps_aux = match.latest_signals.count("cam_02") ? impl_->input_fps : 0;
        status.events_detected = match.events_detected;
        status.focus_region_cam_01_ready = match.focus_region_cam_01_ready;
        status.focus_region_cam_02_ready = match.focus_region_cam_02_ready;
        status.last_program_decision_camera = match.last_program_decision_camera;
        status.error_message = match.error_message;
    } else {
        status.error_message = "match not initialized";
    }
    return status;
}

void VisionService::process_simulated_frames() {
    const int64_t current_ms = now_ms();
    const int frame_interval_ms = std::max(1, 1000 / std::max(1, impl_->input_fps));
    if (current_ms - impl_->last_simulation_tick_ms < frame_interval_ms) {
        return;
    }
    impl_->last_simulation_tick_ms = current_ms;

    std::vector<std::string> running_matches;
    for (const auto& kv : impl_->matches) {
        if (kv.second.running) {
            running_matches.push_back(kv.first);
        }
    }

    for (const auto& match_id : running_matches) {
        InputFrame main_frame;
        main_frame.camera_id = "cam_01";
        main_frame.match_id = match_id;
        main_frame.timestamp_ms = current_ms;
        main_frame.frame_index = impl_->simulation_frame_index;
        main_frame.width = impl_->input_width;
        main_frame.height = impl_->input_height;
        main_frame.fps = impl_->input_fps;
        main_frame.format = FrameFormat::BGR8;
        process_frame(main_frame);

        InputFrame aux_frame = main_frame;
        aux_frame.camera_id = "cam_02";
        process_frame(aux_frame);
    }

    if (!running_matches.empty()) {
        ++impl_->simulation_frame_index;
    }
}

// ============================================================================
// 比赛管理
// ============================================================================

bool VisionService::init_match(const std::string& match_id) {
    Impl::MatchState state;
    state.match_id = match_id;
    state.match_start_timestamp_ms = now_ms();
    state.last_program_decision_camera = "cam_01";
    impl_->matches[match_id] = state;
    std::cout << "[service] Match initialized: " << match_id << std::endl;
    return true;
}

bool VisionService::start_match(const std::string& match_id) {
    auto it = impl_->matches.find(match_id);
    if (it == impl_->matches.end()) {
        return false;
    }
    it->second.running = true;
    it->second.match_start_timestamp_ms = now_ms();
    std::cout << "[service] Match started: " << match_id << std::endl;
    return true;
}

bool VisionService::stop_match(const std::string& match_id) {
    auto it = impl_->matches.find(match_id);
    if (it == impl_->matches.end()) {
        return false;
    }
    it->second.running = false;
    write_event_candidates(match_id);
    std::cout << "[service] Match stopped: " << match_id << std::endl;
    return true;
}

bool VisionService::write_event_candidates(const std::string& match_id) {
    const EventList events = get_event_candidates(match_id);
    const std::string path = impl_->event_candidates_path(match_id);
    create_directory_recursive(directory_name(path));

    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output) {
        auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            it->second.error_message = "failed to write event_candidates.json";
        }
        return false;
    }

    output << "{";
    output << "\"match_id\":\"" << json_escape(events.match_id) << "\",";
    output << "\"events\":[";
    for (size_t i = 0; i < events.events.size(); ++i) {
        if (i > 0) output << ",";
        output << events.events[i].to_json();
    }
    output << "]}";
    return true;
}

// ============================================================================
// 帧处理骨架
// ============================================================================

void VisionService::process_frame(const InputFrame& frame) {
    if (!frame.is_valid()) {
        return;
    }

    auto match_it = impl_->matches.find(frame.match_id);
    if (match_it == impl_->matches.end()) {
        Impl::MatchState state;
        state.match_id = frame.match_id;
        state.running = true;
        state.match_start_timestamp_ms = frame.timestamp_ms > 0 ? frame.timestamp_ms : now_ms();
        state.last_program_decision_camera = "cam_01";
        match_it = impl_->matches.emplace(frame.match_id, state).first;
    }

    auto& match = match_it->second;
    const Impl::FrameSignal signal = impl_->estimate_signal_from_frame(frame);
    match.latest_signals[frame.camera_id] = signal;
    if (frame.camera_id == "cam_01") {
        match.focus_region_cam_01_ready = true;
    } else if (frame.camera_id == "cam_02") {
        match.focus_region_cam_02_ready = true;
    }

    impl_->maybe_add_events(match);
}

// ============================================================================
// 关注区域生成
// ============================================================================

MultiFocusRegion VisionService::generate_focus_regions(const std::string& match_id) {
    MultiFocusRegion result;
    result.match_id = match_id;

    Impl::FrameSignal main_signal = impl_->default_signal("cam_01");
    Impl::FrameSignal aux_signal = impl_->default_signal("cam_02");
    {
        const auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            main_signal = impl_->latest_or_default(it->second, "cam_01");
            aux_signal = impl_->latest_or_default(it->second, "cam_02");
        }
    }

    result.timestamp_ms = std::max(main_signal.timestamp_ms, aux_signal.timestamp_ms);

    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    const double main_scale = main_signal.ball_confidence > 0.45 ? 0.58 : 0.72;
    main_region.rect = make_centered_16_9_rect(main_signal.center_x, main_signal.center_y,
                                               main_signal.width, main_signal.height, main_scale);
    main_region.source_type = main_signal.ball_confidence > 0.45
        ? FocusRegionSource::BALL_DETECTION
        : (main_signal.activity > 0.20 ? FocusRegionSource::MOTION_CLUSTER : FocusRegionSource::DEFAULT);
    main_region.confidence = clamp01(std::max(main_signal.ball_confidence,
                                              main_signal.activity * 0.85 + 0.10));

    FocusRegion aux_region;
    aux_region.camera_id = "cam_02";
    const double aux_scale = aux_signal.goal_activity > 0.60 ? 0.46 : 0.56;
    aux_region.rect = make_centered_16_9_rect(aux_signal.center_x, aux_signal.center_y,
                                             aux_signal.width, aux_signal.height, aux_scale);
    aux_region.source_type = aux_signal.ball_confidence > 0.40
        ? FocusRegionSource::BALL_DETECTION
        : (aux_signal.activity > 0.20 ? FocusRegionSource::MOTION_CLUSTER : FocusRegionSource::DEFAULT);
    aux_region.confidence = clamp01(std::max(aux_signal.ball_confidence,
                                            aux_signal.goal_activity * 0.80 + 0.08));

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

    Impl::FrameSignal main_signal = impl_->default_signal("cam_01");
    Impl::FrameSignal aux_signal = impl_->default_signal("cam_02");
    {
        const auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            main_signal = impl_->latest_or_default(it->second, "cam_01");
            aux_signal = impl_->latest_or_default(it->second, "cam_02");
        }
    }

    decision.timestamp_ms = std::max(main_signal.timestamp_ms, aux_signal.timestamp_ms);

    if (aux_signal.box_activity >= 0.74) {
        decision.recommended_camera_id = "cam_02";
        decision.reason = DecisionReason::SIX_YARD_BOX_HIGHLIGHT;
        decision.confidence = clamp01(aux_signal.box_activity);
    } else if (aux_signal.goal_activity >= 0.66 &&
               aux_signal.goal_activity >= main_signal.goal_activity * 0.95) {
        decision.recommended_camera_id = "cam_02";
        decision.reason = DecisionReason::GOAL_AREA_ACTIVITY_BOOSTED;
        decision.confidence = clamp01(aux_signal.goal_activity);
    } else if (!main_signal.valid && aux_signal.valid) {
        decision.recommended_camera_id = "cam_02";
        decision.reason = DecisionReason::AUX_CAMERA_FALLBACK;
        decision.confidence = 0.60;
    } else if (main_signal.activity >= 0.24 || main_signal.ball_confidence >= 0.30) {
        decision.recommended_camera_id = "cam_01";
        decision.reason = DecisionReason::GLOBAL_PLAY_TRACKING;
        decision.confidence = clamp01(std::max(main_signal.activity, main_signal.ball_confidence));
    } else {
        decision.recommended_camera_id = "cam_01";
        decision.reason = DecisionReason::DEFAULT_MAIN_CAMERA;
        decision.confidence = 0.55;
    }

    {
        const auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            it->second.last_program_decision_camera = decision.recommended_camera_id;
        }
    }

    return decision;
}

// ============================================================================
// 事件检测
// ============================================================================

EventList VisionService::get_event_candidates(const std::string& match_id) {
    EventList result;
    result.match_id = match_id;

    auto it = impl_->matches.find(match_id);
    if (it != impl_->matches.end()) {
        result.events = it->second.events;
    }

    return result;
}

} // namespace vision

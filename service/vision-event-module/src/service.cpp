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
#include "ball_detector.hpp"
#include "motion_analyzer.hpp"
#include "box_activity_analyzer.hpp"

#include <iostream>
#include <atomic>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
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

bool point_in_rect(double x, double y, const Rect& rect) {
    return rect.is_valid() &&
           x >= rect.x && x <= rect.x + rect.width &&
           y >= rect.y && y <= rect.y + rect.height;
}

double rect_area_ratio(const Rect& rect, int frame_width, int frame_height) {
    const double frame_area =
        static_cast<double>(std::max(1, frame_width)) * std::max(1, frame_height);
    return rect.is_valid() ? std::min(1.0, rect.area() / frame_area) : 0.0;
}

Rect scale_rect_to_frame(const Rect& rect,
                         int source_width,
                         int source_height,
                         int frame_width,
                         int frame_height) {
    if (!rect.is_valid()) {
        return rect;
    }
    const double sx = static_cast<double>(std::max(1, frame_width)) / std::max(1, source_width);
    const double sy = static_cast<double>(std::max(1, frame_height)) / std::max(1, source_height);
    Rect scaled;
    scaled.x = clamp_int(static_cast<int>(std::round(rect.x * sx)), 0, std::max(0, frame_width - 1));
    scaled.y = clamp_int(static_cast<int>(std::round(rect.y * sy)), 0, std::max(0, frame_height - 1));
    scaled.width = clamp_int(static_cast<int>(std::round(rect.width * sx)), 1,
                             std::max(1, frame_width - scaled.x));
    scaled.height = clamp_int(static_cast<int>(std::round(rect.height * sy)), 1,
                              std::max(1, frame_height - scaled.y));
    return scaled;
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

bool parse_bool_value(const std::string& value) {
    return value != "false" && value != "0" && value != "no";
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

std::string http_request_body(const std::string& host,
                              const std::string& path,
                              const std::string& json) {
    std::ostringstream oss;
    oss << "POST " << path << " HTTP/1.1\r\n";
    oss << "Host: " << host << "\r\n";
    oss << "Content-Type: application/json; charset=utf-8\r\n";
    oss << "Content-Length: " << json.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << json;
    return oss.str();
}

bool post_json_local(const std::string& host,
                     int port,
                     const std::string& path,
                     const std::string& json,
                     std::string& error_message) {
#ifndef _WIN32
    (void)host;
    (void)port;
    (void)path;
    (void)json;
    error_message = "HTTP push is implemented for Windows native runtime";
    return false;
#else
    WSADATA wsa_data;
    const bool started_winsock = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    if (!started_winsock) {
        error_message = "WSAStartup failed";
        return false;
    }

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        error_message = "socket failed";
        WSACleanup();
        return false;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = inet_addr(host.c_str());
    if (address.sin_addr.s_addr == INADDR_NONE) {
        error_message = "only IPv4 local addresses are supported for output push";
        closesocket(client);
        WSACleanup();
        return false;
    }

    if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        error_message = "connect failed";
        closesocket(client);
        WSACleanup();
        return false;
    }

    const std::string request = http_request_body(host, path, json);
    int sent_total = 0;
    while (sent_total < static_cast<int>(request.size())) {
        const int sent = send(client,
                              request.c_str() + sent_total,
                              static_cast<int>(request.size()) - sent_total,
                              0);
        if (sent <= 0) {
            error_message = "send failed";
            closesocket(client);
            WSACleanup();
            return false;
        }
        sent_total += sent;
    }

    char response[256];
    const int received = recv(client, response, sizeof(response) - 1, 0);
    closesocket(client);
    WSACleanup();
    if (received <= 0) {
        error_message = "empty response";
        return false;
    }
    response[received] = '\0';
    const std::string status_line(response);
    if (status_line.find("HTTP/1.1 2") != 0 && status_line.find("HTTP/1.0 2") != 0) {
        error_message = "non-2xx response";
        return false;
    }
    return true;
#endif
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
    std::string main_stream_uri = "rtsp://127.0.0.1:8554/main";
    std::string aux_stream_uri = "rtsp://127.0.0.1:8555/aux";
    std::string record_host = "127.0.0.1";
    int record_port = 8082;
    std::string ball_model_path = "../models/ball_detector.onnx";
    float ball_confidence_threshold = 0.35f;
    float ball_nms_threshold = 0.45f;
    bool enable_realtime_streams = true;
    bool fallback_to_simulation = false;
    bool push_dual_regions = true;
    bool push_program_decision = true;
    int focus_region_update_ms = 200;
    int stale_stream_timeout_ms = 3000;
    int input_width = kDefaultWidth;
    int input_height = kDefaultHeight;
    int input_fps = 25;
    int64_t simulation_frame_index = 0;
    int64_t last_simulation_tick_ms = 0;
    int64_t realtime_frame_index = 0;
    int64_t last_realtime_tick_ms = 0;

    std::unique_ptr<inference::BallDetector> ball_detector;
    std::map<std::string, std::unique_ptr<inference::MotionAnalyzer>> motion_analyzers;
    std::map<std::string, std::unique_ptr<inference::BoxActivityAnalyzer>> box_analyzers;
    std::map<std::string, cv::VideoCapture> captures;

    struct CameraRoiConfig {
        Rect field_safe;
        Rect default_focus;
        Rect goal_area;
        Rect penalty_area;
        Rect six_yard_box;
        Rect wing_attack;
        int source_width = kDefaultWidth;
        int source_height = kDefaultHeight;
    };
    std::map<std::string, CameraRoiConfig> roi_configs;

    struct BallKalmanConfig {
        double min_update_conf = 0.35;
        double gate_base_px = 180.0;
        double gate_conf_scale_px = 450.0;
        double hard_reset_conf = 0.72;
        int max_predict_frames = 6;
        int reset_confirm_frames = 2;
        int min_missing_before_reset = 3;
        double velocity_blend = 0.25;
        double max_speed_px_s = 1800.0;
        double acceleration_blend = 0.08;
        double max_accel_px_s2 = 3500.0;
        double prediction_lead_s = 0.0;
        double max_extra_lead_s = 0.0;
        double gate_min_px = 45.0;
        double gate_max_px = 260.0;
        double gate_speed_scale = 2.2;
    };

    BallKalmanConfig ball_kalman_config;

    struct BallKalmanFilter {
        bool initialized = false;
        int64_t last_timestamp_ms = 0;
        double x[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // x, y, vx, vy, ax, ay
        double p[6][6] = {};
        BallKalmanConfig config;
        int missing_frames = 0;
        inference::BallDetection pending_reset;
        int pending_reset_count = 0;
        bool has_pending_reset = false;
        bool has_prev_measurement = false;
        bool has_prev_measured_velocity = false;
        double prev_measurement_x = 0.0;
        double prev_measurement_y = 0.0;
        int64_t prev_measurement_timestamp_ms = 0;
        double prev_measured_vx = 0.0;
        double prev_measured_vy = 0.0;

        BallKalmanFilter() = default;

        explicit BallKalmanFilter(const BallKalmanConfig& kalman_config)
            : config(kalman_config) {}

        static void set_identity(double matrix[6][6], double scale) {
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    matrix[r][c] = r == c ? scale : 0.0;
                }
            }
        }

        void clamp_velocity() {
            const double speed = std::hypot(x[2], x[3]);
            if (speed > config.max_speed_px_s) {
                const double scale = config.max_speed_px_s / std::max(speed, 1e-6);
                x[2] *= scale;
                x[3] *= scale;
            }
        }

        void clamp_acceleration() {
            const double accel = std::hypot(x[4], x[5]);
            if (accel > config.max_accel_px_s2) {
                const double scale = config.max_accel_px_s2 / std::max(accel, 1e-6);
                x[4] *= scale;
                x[5] *= scale;
            }
        }

        double lead_time() const {
            const double speed_ratio =
                std::min(1.0, std::hypot(x[2], x[3]) / std::max(config.max_speed_px_s, 1.0));
            return std::max(0.0, config.prediction_lead_s + config.max_extra_lead_s * speed_ratio);
        }

        std::pair<double, double> forecast_position(double lead_s = -1.0) const {
            const double t = lead_s >= 0.0 ? lead_s : lead_time();
            const double forecast_x = x[0] + x[2] * t + 0.5 * x[4] * t * t;
            const double forecast_y = x[1] + x[3] * t + 0.5 * x[5] * t * t;
            return {forecast_x, forecast_y};
        }

        double adaptive_gate_px(double dt, double confidence) const {
            const double speed = std::hypot(x[2], x[3]);
            const double accel = std::hypot(x[4], x[5]);
            const double expected_motion = speed * dt + 0.5 * accel * dt * dt;
            const double confidence_margin = 35.0 * clamp01(confidence);
            const double gate_px =
                config.gate_min_px + expected_motion * config.gate_speed_scale + confidence_margin;
            return std::max(config.gate_min_px, std::min(config.gate_max_px, gate_px));
        }

        void reset(double measured_x, double measured_y, int64_t timestamp_ms) {
            x[0] = measured_x;
            x[1] = measured_y;
            x[2] = 0.0;
            x[3] = 0.0;
            x[4] = 0.0;
            x[5] = 0.0;
            set_identity(p, 80.0);
            last_timestamp_ms = timestamp_ms;
            initialized = true;
            missing_frames = 0;
            has_pending_reset = false;
            pending_reset_count = 0;
            has_prev_measurement = true;
            has_prev_measured_velocity = false;
            prev_measurement_x = measured_x;
            prev_measurement_y = measured_y;
            prev_measurement_timestamp_ms = timestamp_ms;
        }

        void predict(double dt) {
            const double dt2 = dt * dt;
            // Constant-acceleration model, matching the Python tuning script.
            const double f[6][6] = {
                {1.0, 0.0, dt, 0.0, 0.5 * dt2, 0.0},
                {0.0, 1.0, 0.0, dt, 0.0, 0.5 * dt2},
                {0.0, 0.0, 1.0, 0.0, dt, 0.0},
                {0.0, 0.0, 0.0, 1.0, 0.0, dt},
                {0.0, 0.0, 0.0, 0.0, 1.0, 0.0},
                {0.0, 0.0, 0.0, 0.0, 0.0, 1.0}
            };
            clamp_velocity();
            clamp_acceleration();

            double predicted_x[6] = {};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    predicted_x[r] += f[r][c] * x[c];
                }
            }
            for (int i = 0; i < 6; ++i) {
                x[i] = predicted_x[i];
            }

            double fp[6][6] = {};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    for (int k = 0; k < 6; ++k) {
                        fp[r][c] += f[r][k] * p[k][c];
                    }
                }
            }

            double fpf_t[6][6] = {};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    for (int k = 0; k < 6; ++k) {
                        fpf_t[r][c] += fp[r][k] * f[c][k];
                    }
                }
            }

            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    p[r][c] = fpf_t[r][c];
                }
            }
            p[0][0] += 25.0 * dt2 + 1.0;
            p[1][1] += 25.0 * dt2 + 1.0;
            p[2][2] += 90.0 * dt + 1.0;
            p[3][3] += 90.0 * dt + 1.0;
            p[4][4] += 260.0 * dt + 1.0;
            p[5][5] += 260.0 * dt + 1.0;
            clamp_velocity();
            clamp_acceleration();
        }

        void update(double measured_x, double measured_y, double confidence) {
            const double measurement_noise =
                std::max(18.0, 170.0 * (1.0 - clamp01(confidence)) + 18.0);
            const double innovation[2] = {
                measured_x - x[0],
                measured_y - x[1]
            };
            const double s00 = p[0][0] + measurement_noise;
            const double s01 = p[0][1];
            const double s10 = p[1][0];
            const double s11 = p[1][1] + measurement_noise;
            const double det = s00 * s11 - s01 * s10;
            if (std::abs(det) < 1e-6) {
                return;
            }

            const double inv_s00 = s11 / det;
            const double inv_s01 = -s01 / det;
            const double inv_s10 = -s10 / det;
            const double inv_s11 = s00 / det;

            double k[6][2] = {};
            for (int r = 0; r < 6; ++r) {
                k[r][0] = p[r][0] * inv_s00 + p[r][1] * inv_s10;
                k[r][1] = p[r][0] * inv_s01 + p[r][1] * inv_s11;
            }

            for (int r = 0; r < 6; ++r) {
                x[r] += k[r][0] * innovation[0] + k[r][1] * innovation[1];
            }

            double kh[6][6] = {};
            for (int r = 0; r < 6; ++r) {
                kh[r][0] = k[r][0];
                kh[r][1] = k[r][1];
            }
            double i_minus_kh[6][6] = {};
            set_identity(i_minus_kh, 1.0);
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    i_minus_kh[r][c] -= kh[r][c];
                }
            }

            double updated_p[6][6] = {};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    for (int n = 0; n < 6; ++n) {
                        updated_p[r][c] += i_minus_kh[r][n] * p[n][c];
                    }
                }
            }
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    p[r][c] = updated_p[r][c];
                }
            }
            clamp_velocity();
            clamp_acceleration();
        }

        void update_velocity_from_measurement(const inference::BallDetection& detection,
                                              int64_t timestamp_ms) {
            if (!has_prev_measurement) {
                has_prev_measurement = true;
                prev_measurement_x = detection.x;
                prev_measurement_y = detection.y;
                prev_measurement_timestamp_ms = timestamp_ms;
                return;
            }

            const double dt = std::max(
                0.001, (timestamp_ms - prev_measurement_timestamp_ms) / 1000.0);
            const double measured_vx = (detection.x - prev_measurement_x) / dt;
            const double measured_vy = (detection.y - prev_measurement_y) / dt;
            double measured_ax = (measured_vx - x[2]) / dt;
            double measured_ay = (measured_vy - x[3]) / dt;
            if (has_prev_measured_velocity) {
                measured_ax = (measured_vx - prev_measured_vx) / dt;
                measured_ay = (measured_vy - prev_measured_vy) / dt;
            }

            x[2] = x[2] * (1.0 - config.velocity_blend) + measured_vx * config.velocity_blend;
            x[3] = x[3] * (1.0 - config.velocity_blend) + measured_vy * config.velocity_blend;
            x[4] = x[4] * (1.0 - config.acceleration_blend) +
                   measured_ax * config.acceleration_blend;
            x[5] = x[5] * (1.0 - config.acceleration_blend) +
                   measured_ay * config.acceleration_blend;
            clamp_velocity();
            clamp_acceleration();

            prev_measurement_x = detection.x;
            prev_measurement_y = detection.y;
            prev_measurement_timestamp_ms = timestamp_ms;
            prev_measured_vx = measured_vx;
            prev_measured_vy = measured_vy;
            has_prev_measured_velocity = true;
        }

        bool update_pending_reset(const inference::BallDetection& detection) {
            if (missing_frames < config.min_missing_before_reset ||
                detection.confidence < config.hard_reset_conf) {
                has_pending_reset = false;
                pending_reset_count = 0;
                return false;
            }

            if (!has_pending_reset) {
                pending_reset = detection;
                pending_reset_count = 1;
                has_pending_reset = true;
                return pending_reset_count >= config.reset_confirm_frames;
            }

            const double distance =
                std::hypot(detection.x - pending_reset.x, detection.y - pending_reset.y);
            if (distance <= std::max(80.0, std::min(220.0, config.gate_base_px))) {
                pending_reset = detection;
                ++pending_reset_count;
            } else {
                pending_reset = detection;
                pending_reset_count = 1;
            }
            return pending_reset_count >= config.reset_confirm_frames;
        }

        inference::BallDetection no_detection() const {
            return inference::BallDetection();
        }

        inference::BallDetection smooth(const inference::BallDetection& detection,
                                        int64_t timestamp_ms,
                                        int frame_width,
                                        int frame_height) {
            // Missing/rejected detections advance internal state only. Production
            // output does not emit synthetic ball boxes, preserving the frozen
            // event/focus contracts and avoiding false-positive events.
            if (!detection.detected) {
                if (initialized) {
                    const double dt = std::max(
                        0.001,
                        std::min(0.25, (timestamp_ms - last_timestamp_ms) / 1000.0));
                    predict(dt);
                    last_timestamp_ms = timestamp_ms;
                    ++missing_frames;
                }
                return detection;
            }

            if (detection.confidence < config.min_update_conf) {
                if (initialized) {
                    const double dt = std::max(
                        0.001,
                        std::min(0.25, (timestamp_ms - last_timestamp_ms) / 1000.0));
                    predict(dt);
                    last_timestamp_ms = timestamp_ms;
                    ++missing_frames;
                }
                return no_detection();
            }

            if (!initialized) {
                reset(detection.x, detection.y, timestamp_ms);
            } else {
                const double dt = std::max(
                    0.001,
                    std::min(0.25, (timestamp_ms - last_timestamp_ms) / 1000.0));
                predict(dt);
                const double distance = std::hypot(detection.x - x[0], detection.y - x[1]);
                const double legacy_gate_px =
                    config.gate_base_px + config.gate_conf_scale_px * detection.confidence;
                const double gate_px =
                    std::min(legacy_gate_px, adaptive_gate_px(dt, detection.confidence));
                if (distance > gate_px) {
                    if (update_pending_reset(detection)) {
                        reset(detection.x, detection.y, timestamp_ms);
                    } else {
                        ++missing_frames;
                        last_timestamp_ms = timestamp_ms;
                        return no_detection();
                    }
                } else {
                    missing_frames = 0;
                    has_pending_reset = false;
                    pending_reset_count = 0;
                    update(detection.x, detection.y, detection.confidence);
                    update_velocity_from_measurement(detection, timestamp_ms);
                    last_timestamp_ms = timestamp_ms;
                }
            }

            if (!has_prev_measurement ||
                prev_measurement_timestamp_ms != timestamp_ms ||
                prev_measurement_x != detection.x ||
                prev_measurement_y != detection.y) {
                update(detection.x, detection.y, detection.confidence);
                update_velocity_from_measurement(detection, timestamp_ms);
                last_timestamp_ms = timestamp_ms;
            }

            inference::BallDetection smoothed = detection;
            const auto forecast = forecast_position();
            smoothed.x = static_cast<float>(
                clamp01(forecast.first / std::max(1, frame_width)) * std::max(1, frame_width));
            smoothed.y = static_cast<float>(
                clamp01(forecast.second / std::max(1, frame_height)) * std::max(1, frame_height));
            smoothed.confidence = static_cast<float>(
                clamp01(detection.confidence * 0.85 + 0.15));
            return smoothed;
        }
    };
    std::map<std::string, BallKalmanFilter> ball_filters;

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
        int64_t last_focus_region_timestamp_ms = 0;
        int64_t last_decision_timestamp_ms = 0;
        int64_t last_output_push_timestamp_ms = 0;
        bool degraded = false;
        bool decision_push_initialized = false;
        std::string last_pushed_decision_camera;
        DecisionReason last_pushed_decision_reason = DecisionReason::DEFAULT_MAIN_CAMERA;
        double last_pushed_decision_confidence = 0.0;
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

    static int leading_spaces(const std::string& line) {
        int count = 0;
        while (count < static_cast<int>(line.size()) && line[count] == ' ') {
            ++count;
        }
        return count;
    }

    CameraRoiConfig default_roi_config(const std::string& camera_id) const {
        CameraRoiConfig roi;
        roi.field_safe = Rect{0, 0, kDefaultWidth, kDefaultHeight};
        roi.default_focus = Rect{240, 135, 1440, 810};
        if (camera_id == "cam_02") {
            roi.goal_area = Rect{360, 140, 1120, 760};
            roi.penalty_area = Rect{260, 80, 1360, 900};
            roi.six_yard_box = Rect{560, 260, 720, 460};
            roi.wing_attack = Rect{0, 120, 760, 840};
        } else {
            roi.goal_area = Rect{1380, 220, 420, 620};
            roi.penalty_area = Rect{1120, 160, 720, 760};
            roi.six_yard_box = Rect{1480, 350, 300, 360};
            roi.wing_attack = Rect{980, 0, 820, 1080};
        }
        return roi;
    }

    Rect configured_roi(const std::string& camera_id,
                        const std::string& roi_name,
                        int frame_width,
                        int frame_height) const {
        CameraRoiConfig config = default_roi_config(camera_id);
        const auto it = roi_configs.find(camera_id);
        if (it != roi_configs.end()) {
            config = it->second;
        }

        Rect roi;
        if (roi_name == "field_safe") roi = config.field_safe;
        else if (roi_name == "default_focus") roi = config.default_focus;
        else if (roi_name == "goal_area") roi = config.goal_area;
        else if (roi_name == "penalty_area") roi = config.penalty_area;
        else if (roi_name == "six_yard_box") roi = config.six_yard_box;
        else if (roi_name == "wing_attack") roi = config.wing_attack;
        else roi = config.default_focus;

        return scale_rect_to_frame(
            roi, config.source_width, config.source_height, frame_width, frame_height);
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
        std::string roi_camera;
        std::string roi_name;
        while (std::getline(input, line)) {
            const std::string stripped = trim(line);
            if (stripped.empty() || stripped[0] == '#') continue;

            const bool top_level = !line.empty() && line[0] != ' ' && line[0] != '\t';
            if (top_level && stripped.back() == ':') {
                section = stripped.substr(0, stripped.size() - 1);
                subsection.clear();
                roi_camera.clear();
                roi_name.clear();
                continue;
            }
            if (section == "roi" && stripped.back() == ':') {
                const int indent = leading_spaces(line);
                if (indent == 2) {
                    roi_camera = stripped.substr(0, stripped.size() - 1);
                    roi_name.clear();
                    if (roi_configs.find(roi_camera) == roi_configs.end()) {
                        roi_configs[roi_camera] = default_roi_config(roi_camera);
                    }
                    continue;
                }
                if (indent == 4) {
                    roi_name = stripped.substr(0, stripped.size() - 1);
                    continue;
                }
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
            } else if (section == "output" && stripped.find("record_host:") == 0) {
                record_host = value_after_colon(stripped);
            } else if (section == "output" && stripped.find("record_port:") == 0) {
                record_port = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "output" && stripped.find("focus_region_update_ms:") == 0) {
                focus_region_update_ms = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "output" && stripped.find("push_dual_regions:") == 0) {
                push_dual_regions = parse_bool_value(value_after_colon(stripped));
            } else if (section == "output" && stripped.find("push_program_decision:") == 0) {
                push_program_decision = parse_bool_value(value_after_colon(stripped));
            } else if (section == "output" && stripped.find("stale_stream_timeout_ms:") == 0) {
                stale_stream_timeout_ms = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "input" && subsection == "main_camera" &&
                       stripped.find("stream_uri:") == 0) {
                main_stream_uri = value_after_colon(stripped);
            } else if (section == "input" && subsection == "aux_camera" &&
                       stripped.find("stream_uri:") == 0) {
                aux_stream_uri = value_after_colon(stripped);
            } else if (section == "input" && stripped.find("enable_realtime_streams:") == 0) {
                enable_realtime_streams = parse_bool_value(value_after_colon(stripped));
            } else if (section == "input" && stripped.find("fallback_to_simulation:") == 0) {
                fallback_to_simulation = parse_bool_value(value_after_colon(stripped));
            } else if (section == "input" && stripped.find("width:") == 0) {
                input_width = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "input" && stripped.find("height:") == 0) {
                input_height = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "input" && stripped.find("fps:") == 0) {
                input_fps = std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "ball_detector" &&
                       stripped.find("model_path:") == 0) {
                ball_model_path = value_after_colon(stripped);
            } else if (section == "inference" && subsection == "ball_detector" &&
                       stripped.find("confidence_threshold:") == 0) {
                ball_confidence_threshold =
                    static_cast<float>(std::atof(value_after_colon(stripped).c_str()));
            } else if (section == "inference" && subsection == "ball_detector" &&
                       stripped.find("nms_threshold:") == 0) {
                ball_nms_threshold =
                    static_cast<float>(std::atof(value_after_colon(stripped).c_str()));
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("min_update_conf:") == 0) {
                ball_kalman_config.min_update_conf =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("gate_base_px:") == 0) {
                ball_kalman_config.gate_base_px =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("gate_conf_scale_px:") == 0) {
                ball_kalman_config.gate_conf_scale_px =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("hard_reset_conf:") == 0) {
                ball_kalman_config.hard_reset_conf =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("max_predict_frames:") == 0) {
                ball_kalman_config.max_predict_frames =
                    std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("reset_confirm_frames:") == 0) {
                ball_kalman_config.reset_confirm_frames =
                    std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("min_missing_before_reset:") == 0) {
                ball_kalman_config.min_missing_before_reset =
                    std::atoi(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("velocity_blend:") == 0) {
                ball_kalman_config.velocity_blend =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("max_speed_px_s:") == 0) {
                ball_kalman_config.max_speed_px_s =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("acceleration_blend:") == 0) {
                ball_kalman_config.acceleration_blend =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("max_accel_px_s2:") == 0) {
                ball_kalman_config.max_accel_px_s2 =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("prediction_lead_s:") == 0) {
                ball_kalman_config.prediction_lead_s =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("max_extra_lead_s:") == 0) {
                ball_kalman_config.max_extra_lead_s =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("gate_min_px:") == 0) {
                ball_kalman_config.gate_min_px =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("gate_max_px:") == 0) {
                ball_kalman_config.gate_max_px =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "inference" && subsection == "kalman_filter" &&
                       stripped.find("gate_speed_scale:") == 0) {
                ball_kalman_config.gate_speed_scale =
                    std::atof(value_after_colon(stripped).c_str());
            } else if (section == "roi" && !roi_camera.empty() && !roi_name.empty()) {
                Rect* target = nullptr;
                CameraRoiConfig& roi_config = roi_configs[roi_camera];
                if (roi_name == "field_safe") target = &roi_config.field_safe;
                else if (roi_name == "default_focus") target = &roi_config.default_focus;
                else if (roi_name == "goal_area") target = &roi_config.goal_area;
                else if (roi_name == "penalty_area") target = &roi_config.penalty_area;
                else if (roi_name == "six_yard_box") target = &roi_config.six_yard_box;
                else if (roi_name == "wing_attack") target = &roi_config.wing_attack;

                if (target && stripped.find("x:") == 0) {
                    target->x = std::atoi(value_after_colon(stripped).c_str());
                } else if (target && stripped.find("y:") == 0) {
                    target->y = std::atoi(value_after_colon(stripped).c_str());
                } else if (target && stripped.find("width:") == 0) {
                    target->width = std::atoi(value_after_colon(stripped).c_str());
                } else if (target && stripped.find("height:") == 0) {
                    target->height = std::atoi(value_after_colon(stripped).c_str());
                }
            }
        }

        if (http_port <= 0) http_port = 8083;
        if (http_host.empty()) http_host = "127.0.0.1";
        if (record_port <= 0) record_port = 8082;
        if (record_host.empty()) record_host = "127.0.0.1";
        if (default_match_id.empty()) default_match_id = "match_20260405_001";
        if (metadata_root.empty()) metadata_root = "../data/metadata";
        if (!is_absolute_path(metadata_root)) {
            metadata_root = join_path(directory_name(config_path), metadata_root);
        }
        if (!is_absolute_path(ball_model_path)) {
            ball_model_path = join_path(directory_name(config_path), ball_model_path);
        }
        if (input_width <= 0) input_width = kDefaultWidth;
        if (input_height <= 0) input_height = kDefaultHeight;
        if (input_fps <= 0) input_fps = 25;
        if (focus_region_update_ms <= 0) focus_region_update_ms = 200;
        if (stale_stream_timeout_ms <= 0) stale_stream_timeout_ms = 3000;
        if (roi_configs.find("cam_01") == roi_configs.end()) {
            roi_configs["cam_01"] = default_roi_config("cam_01");
        }
        if (roi_configs.find("cam_02") == roi_configs.end()) {
            roi_configs["cam_02"] = default_roi_config("cam_02");
        }
        ball_kalman_config.min_update_conf = clamp01(ball_kalman_config.min_update_conf);
        ball_kalman_config.hard_reset_conf = clamp01(ball_kalman_config.hard_reset_conf);
        ball_kalman_config.velocity_blend = clamp01(ball_kalman_config.velocity_blend);
        ball_kalman_config.acceleration_blend = clamp01(ball_kalman_config.acceleration_blend);
        ball_kalman_config.max_predict_frames =
            std::max(0, ball_kalman_config.max_predict_frames);
        ball_kalman_config.reset_confirm_frames =
            std::max(1, ball_kalman_config.reset_confirm_frames);
        ball_kalman_config.min_missing_before_reset =
            std::max(0, ball_kalman_config.min_missing_before_reset);
        ball_kalman_config.max_speed_px_s =
            std::max(1.0, ball_kalman_config.max_speed_px_s);
        ball_kalman_config.max_accel_px_s2 =
            std::max(1.0, ball_kalman_config.max_accel_px_s2);
        ball_kalman_config.prediction_lead_s =
            std::max(0.0, ball_kalman_config.prediction_lead_s);
        ball_kalman_config.max_extra_lead_s =
            std::max(0.0, ball_kalman_config.max_extra_lead_s);
        ball_kalman_config.gate_min_px =
            std::max(1.0, ball_kalman_config.gate_min_px);
        ball_kalman_config.gate_max_px =
            std::max(ball_kalman_config.gate_min_px, ball_kalman_config.gate_max_px);
        ball_kalman_config.gate_speed_scale =
            std::max(0.0, ball_kalman_config.gate_speed_scale);
        return true;
    }

    bool initialize_inference() {
        // Build one shared YOLO detector and per-camera lightweight analyzers.
        // Motion/box analyzers keep camera-specific temporal state.
        ball_detector = std::make_unique<inference::BallDetector>(
            ball_model_path, ball_confidence_threshold, ball_nms_threshold);
        if (!ball_detector->initialize()) {
            std::cerr << "[service] Ball detector initialization failed" << std::endl;
            return false;
        }

        motion_analyzers["cam_01"] = std::make_unique<inference::MotionAnalyzer>();
        motion_analyzers["cam_02"] = std::make_unique<inference::MotionAnalyzer>();
        box_analyzers["cam_01"] = std::make_unique<inference::BoxActivityAnalyzer>();
        box_analyzers["cam_02"] = std::make_unique<inference::BoxActivityAnalyzer>();
        return motion_analyzers["cam_01"]->initialize() &&
               motion_analyzers["cam_02"]->initialize() &&
               box_analyzers["cam_01"]->initialize() &&
               box_analyzers["cam_02"]->initialize();
    }

    bool ensure_capture_open(const std::string& camera_id, const std::string& stream_uri) {
        // Lazily open RTSP streams so the service can start before A module has
        // fully published both feeds. Failed reads release and retry later.
        auto& capture = captures[camera_id];
        if (capture.isOpened()) {
            return true;
        }
        if (stream_uri.empty()) {
            return false;
        }

        std::cout << "[service] Opening " << camera_id << " stream: " << stream_uri << std::endl;
        if (!capture.open(stream_uri)) {
            std::cerr << "[service] Failed to open " << camera_id << " stream: "
                      << stream_uri << std::endl;
            return false;
        }
        return true;
    }

    bool read_stream_frame(const std::string& camera_id,
                           const std::string& match_id,
                           const std::string& stream_uri,
                           int64_t timestamp_ms,
                           int64_t frame_index,
                           cv::Mat& image,
                           InputFrame& frame) {
        // Convert OpenCV frames into the module-neutral InputFrame structure.
        // The cv::Mat must stay alive until process_frame() returns.
        if (!ensure_capture_open(camera_id, stream_uri)) {
            return false;
        }

        if (!captures[camera_id].read(image) || image.empty()) {
            captures[camera_id].release();
            std::cerr << "[service] Failed to read frame from " << camera_id << std::endl;
            return false;
        }
        if (image.channels() == 1) {
            cv::Mat converted;
            cv::cvtColor(image, converted, cv::COLOR_GRAY2BGR);
            image = converted;
        } else if (image.channels() == 4) {
            cv::Mat converted;
            cv::cvtColor(image, converted, cv::COLOR_BGRA2BGR);
            image = converted;
        } else if (image.channels() != 3) {
            std::cerr << "[service] Unsupported frame channel count from "
                      << camera_id << ": " << image.channels() << std::endl;
            return false;
        }

        frame.camera_id = camera_id;
        frame.match_id = match_id;
        frame.timestamp_ms = timestamp_ms;
        frame.frame_index = frame_index;
        frame.width = image.cols;
        frame.height = image.rows;
        frame.fps = input_fps;
        frame.format = FrameFormat::BGR8;
        frame.image = image.data;
        return true;
    }

    std::string event_candidates_path(const std::string& match_id) const {
        return metadata_root + "/" + match_id + "/event_candidates.json";
    }

    FrameSignal estimate_signal_from_frame(const InputFrame& frame) {
        // FrameSignal is the internal bridge between raw vision inference and
        // frozen external outputs: focus regions, events, and camera decision.
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

        if (frame.image && ball_detector && ball_detector->is_initialized() &&
            (frame.format == FrameFormat::BGR8 || frame.format == FrameFormat::RGB8)) {
            // YOLO returns one best ball box. Kalman then smooths center jitter
            // per camera before the coordinates drive focus/event rules.
            inference::BallDetection ball =
                ball_detector->detect(frame.image, signal.width, signal.height);
            auto filter_it = ball_filters.find(frame.camera_id);
            if (filter_it == ball_filters.end()) {
                filter_it = ball_filters.emplace(
                    frame.camera_id, BallKalmanFilter(ball_kalman_config)).first;
            }
            ball = filter_it->second.smooth(
                ball, signal.timestamp_ms, signal.width, signal.height);
            if (ball.detected) {
                signal.center_x = ball.x;
                signal.center_y = ball.y;
                signal.ball_confidence = clamp01(ball.confidence);
            }
        }

        auto motion_it = motion_analyzers.find(frame.camera_id);
        if (frame.image && motion_it != motion_analyzers.end() && motion_it->second) {
            // Motion cluster is a fallback/augmenting signal when ball detection
            // is weak or when overall play movement matters more than the ball.
            const inference::MotionAnalysis motion =
                motion_it->second->analyze(frame.image, signal.width, signal.height);
            if (motion.has_significant_motion) {
                signal.center_x = motion.motion_cluster_rect.x + motion.motion_cluster_rect.width / 2.0;
                signal.center_y = motion.motion_cluster_rect.y + motion.motion_cluster_rect.height / 2.0;
                signal.activity = clamp01(std::max(signal.activity, motion.global_activity));
            }
        }

        auto box_it = box_analyzers.find(frame.camera_id);
        if (frame.image && box_it != box_analyzers.end() && box_it->second) {
            // Box activity boosts goal/shot/danger signals near the goal-side ROI.
            const inference::BoxActivity box = box_it->second->analyze(
                frame.image,
                signal.width,
                signal.height,
                static_cast<float>(signal.center_x),
                static_cast<float>(signal.center_y));
            signal.box_activity = clamp01(std::max(signal.box_activity, box.intensity));
        }

        const Rect goal_roi =
            configured_roi(frame.camera_id, "goal_area", signal.width, signal.height);
        const Rect penalty_roi =
            configured_roi(frame.camera_id, "penalty_area", signal.width, signal.height);
        const Rect six_yard_roi =
            configured_roi(frame.camera_id, "six_yard_box", signal.width, signal.height);
        const bool ball_in_goal_roi = point_in_rect(signal.center_x, signal.center_y, goal_roi);
        const bool ball_in_penalty_roi = point_in_rect(signal.center_x, signal.center_y, penalty_roi);
        const bool ball_in_six_yard_roi =
            point_in_rect(signal.center_x, signal.center_y, six_yard_roi);
        const bool aux_goal_view = frame.camera_id == "cam_02";
        const double penalty_area_weight =
            ball_in_penalty_roi ? 0.18 + rect_area_ratio(penalty_roi, signal.width, signal.height) * 0.10 : 0.0;
        signal.goal_activity = clamp01((ball_in_goal_roi ? 0.45 : 0.0) +
                                       penalty_area_weight +
                                       (aux_goal_view ? 0.18 : 0.0) +
                                       signal.activity * 0.55 +
                                       signal.ball_confidence * 0.35);
        signal.box_activity = clamp01(std::max(
            signal.box_activity,
            (aux_goal_view ? 0.22 : 0.0) +
            (ball_in_six_yard_roi ? 0.28 : 0.0) +
            signal.activity * 0.70 +
            signal.goal_activity * 0.35));
        signal.valid = true;
        return signal;
    }

    FrameSignal default_signal(const std::string& camera_id) const {
        FrameSignal signal;
        signal.camera_id = camera_id;
        signal.timestamp_ms = now_ms();
        signal.width = kDefaultWidth;
        signal.height = kDefaultHeight;
        const Rect default_focus =
            configured_roi(camera_id, "default_focus", kDefaultWidth, kDefaultHeight);
        signal.center_x = default_focus.x + default_focus.width / 2.0;
        signal.center_y = default_focus.y + default_focus.height / 2.0;
        signal.activity = 0.15;
        signal.goal_activity = camera_id == "cam_02" ? 0.28 : 0.12;
        signal.box_activity = camera_id == "cam_02" ? 0.30 : 0.10;
        signal.ball_confidence = 0.0;
        signal.valid = false;
        return signal;
    }

    FrameSignal latest_or_default(const MatchState& match,
                                  const std::string& camera_id,
                                  bool enforce_stale_timeout = false) const {
        const auto it = match.latest_signals.find(camera_id);
        if (it != match.latest_signals.end() && it->second.valid &&
            (!enforce_stale_timeout ||
             now_ms() - it->second.timestamp_ms <= stale_stream_timeout_ms)) {
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

    if (!impl_->initialize_inference()) {
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
    impl_->last_realtime_tick_ms = impl_->last_simulation_tick_ms;
    while (impl_->running.load()) {
        // One loop handles both control-plane HTTP and data-plane frame ingest.
        // Real RTSP frames are preferred; simulation remains a local fallback.
        if (impl_->http_server) {
            impl_->http_server->poll_once(20);
        } else {
            sleep_ms(20);
        }
        bool processed_realtime = false;
        if (impl_->enable_realtime_streams) {
            processed_realtime = process_realtime_stream_frames();
        }
        if (!processed_realtime && impl_->fallback_to_simulation) {
            process_simulated_frames();
        }
        publish_outputs();
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
        const int64_t current_ms = now_ms();
        const auto main_signal = match.latest_signals.find("cam_01");
        const auto aux_signal = match.latest_signals.find("cam_02");
        const bool main_online =
            main_signal != match.latest_signals.end() &&
            current_ms - main_signal->second.timestamp_ms <= impl_->stale_stream_timeout_ms;
        const bool aux_online =
            aux_signal != match.latest_signals.end() &&
            current_ms - aux_signal->second.timestamp_ms <= impl_->stale_stream_timeout_ms;

        if (match.running && (match.degraded || !main_online || !aux_online)) {
            status.status = ModuleState::DEGRADED;
        } else {
            status.status = match.running ? ModuleState::RUNNING : impl_->state.load();
        }
        status.camera_main_status = main_online ? "online" : "offline";
        status.camera_aux_status = aux_online ? "online" : "offline";
        status.fps_main = main_online ? impl_->input_fps : 0;
        status.fps_aux = aux_online ? impl_->input_fps : 0;
        status.events_detected = match.events_detected;
        status.focus_region_cam_01_ready = match.focus_region_cam_01_ready && main_online;
        status.focus_region_cam_02_ready = match.focus_region_cam_02_ready && aux_online;
        status.last_program_decision_camera = match.last_program_decision_camera;
        status.last_focus_region_timestamp_ms = match.last_focus_region_timestamp_ms;
        status.last_decision_timestamp_ms = match.last_decision_timestamp_ms;
        status.error_message = match.error_message;
    } else {
        status.error_message = "match not initialized";
    }
    return status;
}

void VisionService::process_simulated_frames() {
    // Simulation keeps API and downstream integration testable without A module,
    // RTSP streams, or a loaded model. It is not the production data path.
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

bool VisionService::process_realtime_stream_frames() {
    // Pull one frame from each configured stream at the target FPS. Each camera
    // is processed independently so a single bad stream does not stop the other.
    const int64_t current_ms = now_ms();
    const int frame_interval_ms = std::max(1, 1000 / std::max(1, impl_->input_fps));
    if (current_ms - impl_->last_realtime_tick_ms < frame_interval_ms) {
        return true;
    }
    impl_->last_realtime_tick_ms = current_ms;

    std::vector<std::string> running_matches;
    for (const auto& kv : impl_->matches) {
        if (kv.second.running) {
            running_matches.push_back(kv.first);
        }
    }
    if (running_matches.empty()) {
        return true;
    }

    bool processed_any = false;
    for (const auto& match_id : running_matches) {
        cv::Mat main_image;
        InputFrame main_frame;
        if (impl_->read_stream_frame("cam_01", match_id, impl_->main_stream_uri,
                                     current_ms, impl_->realtime_frame_index,
                                     main_image, main_frame)) {
            process_frame(main_frame);
            processed_any = true;
        } else {
            auto it = impl_->matches.find(match_id);
            if (it != impl_->matches.end()) {
                it->second.error_message = "cam_01 RTSP input unavailable";
                it->second.degraded = true;
            }
        }

        cv::Mat aux_image;
        InputFrame aux_frame;
        if (impl_->read_stream_frame("cam_02", match_id, impl_->aux_stream_uri,
                                     current_ms, impl_->realtime_frame_index,
                                     aux_image, aux_frame)) {
            process_frame(aux_frame);
            processed_any = true;
        } else {
            auto it = impl_->matches.find(match_id);
            if (it != impl_->matches.end() && it->second.error_message.empty()) {
                it->second.error_message = "cam_02 RTSP input unavailable";
                it->second.degraded = true;
            }
        }
    }

    if (processed_any) {
        ++impl_->realtime_frame_index;
    }
    return processed_any;
}

void VisionService::publish_outputs() {
    const int64_t current_ms = now_ms();
    std::vector<std::string> running_matches;
    for (const auto& kv : impl_->matches) {
        if (kv.second.running) {
            running_matches.push_back(kv.first);
        }
    }

    for (const auto& match_id : running_matches) {
        auto match_it = impl_->matches.find(match_id);
        if (match_it == impl_->matches.end()) {
            continue;
        }
        auto& match = match_it->second;
        if (current_ms - match.last_output_push_timestamp_ms < impl_->focus_region_update_ms) {
            continue;
        }
        match.last_output_push_timestamp_ms = current_ms;
        if (match.latest_signals.empty()) {
            match.error_message = "no realtime frame available for output push";
            match.degraded = true;
            continue;
        }

        if (impl_->push_dual_regions) {
            const MultiFocusRegion regions = generate_focus_regions(match_id);
            if (regions.is_valid()) {
                std::string error_message;
                const std::string path =
                    "/api/v1/record/matches/" + match_id + "/focus-regions";
                if (!post_json_local(impl_->record_host,
                                     impl_->record_port,
                                     path,
                                     regions.to_json(),
                                     error_message)) {
                    match.error_message = "focus-regions push failed: " + error_message;
                    match.degraded = true;
                }
            } else {
                match.error_message = "generated invalid focus-regions";
                match.degraded = true;
            }
        }

        if (impl_->push_program_decision) {
            const ProgramDecision decision = generate_program_decision(match_id);
            const bool reason_changed =
                !match.decision_push_initialized ||
                decision.recommended_camera_id != match.last_pushed_decision_camera ||
                decision.reason != match.last_pushed_decision_reason;
            const bool confidence_crossed_threshold =
                !match.decision_push_initialized ||
                std::abs(decision.confidence - match.last_pushed_decision_confidence) >= 0.10;
            if (decision.is_valid() && (reason_changed || confidence_crossed_threshold)) {
                std::string error_message;
                const std::string path =
                    "/api/v1/record/matches/" + match_id + "/program-decision";
                if (post_json_local(impl_->record_host,
                                    impl_->record_port,
                                    path,
                                    decision.to_json(),
                                    error_message)) {
                    match.decision_push_initialized = true;
                    match.last_pushed_decision_camera = decision.recommended_camera_id;
                    match.last_pushed_decision_reason = decision.reason;
                    match.last_pushed_decision_confidence = decision.confidence;
                } else {
                    match.error_message = "program-decision push failed: " + error_message;
                    match.degraded = true;
                }
            }
        }
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
    state.degraded = false;
    impl_->matches[match_id] = state;
    impl_->ball_filters["cam_01"] = Impl::BallKalmanFilter(impl_->ball_kalman_config);
    impl_->ball_filters["cam_02"] = Impl::BallKalmanFilter(impl_->ball_kalman_config);
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
    it->second.degraded = false;
    it->second.error_message.clear();
    std::cout << "[service] Match started: " << match_id << std::endl;
    return true;
}

bool VisionService::configure_stream(const std::string& camera_id, const std::string& stream_uri) {
    if (stream_uri.empty()) {
        return false;
    }
    if (camera_id == "cam_01") {
        impl_->main_stream_uri = stream_uri;
    } else if (camera_id == "cam_02") {
        impl_->aux_stream_uri = stream_uri;
    } else {
        return false;
    }

    auto capture_it = impl_->captures.find(camera_id);
    if (capture_it != impl_->captures.end()) {
        capture_it->second.release();
    }
    std::cout << "[service] Stream configured: " << camera_id
              << " -> " << stream_uri << std::endl;
    return true;
}

bool VisionService::stop_match(const std::string& match_id) {
    auto it = impl_->matches.find(match_id);
    if (it == impl_->matches.end()) {
        return false;
    }
    it->second.running = false;
    const bool written = write_event_candidates(match_id);
    std::cout << "[service] Match stopped: " << match_id << std::endl;
    return written;
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
    // This is the only frame ingestion point used by both realtime and simulated
    // paths, which keeps event/focus/decision behavior consistent.
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
    // Always return cam_01 and cam_02 regions. Missing or invalid signals fall
    // back per camera instead of disabling the whole dual-camera output.
    MultiFocusRegion result;
    result.match_id = match_id;

    Impl::FrameSignal main_signal = impl_->default_signal("cam_01");
    Impl::FrameSignal aux_signal = impl_->default_signal("cam_02");
    {
        const auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            main_signal = impl_->latest_or_default(it->second, "cam_01", true);
            aux_signal = impl_->latest_or_default(it->second, "cam_02", true);
        }
    }

    result.timestamp_ms = std::max(main_signal.timestamp_ms, aux_signal.timestamp_ms);

    FocusRegion main_region;
    main_region.camera_id = "cam_01";
    const double main_scale = main_signal.ball_confidence > 0.45 ? 0.58 : 0.72;
    main_region.rect = main_signal.valid
        ? make_centered_16_9_rect(main_signal.center_x, main_signal.center_y,
                                  main_signal.width, main_signal.height, main_scale)
        : impl_->configured_roi("cam_01", "default_focus", main_signal.width, main_signal.height);
    main_region.source_type = main_signal.ball_confidence > 0.45
        ? FocusRegionSource::BALL_DETECTION
        : (main_signal.activity > 0.20 ? FocusRegionSource::MOTION_CLUSTER : FocusRegionSource::DEFAULT);
    main_region.confidence = clamp01(std::max(main_signal.ball_confidence,
                                              main_signal.activity * 0.85 + 0.10));

    FocusRegion aux_region;
    aux_region.camera_id = "cam_02";
    const double aux_scale = aux_signal.goal_activity > 0.60 ? 0.46 : 0.56;
    aux_region.rect = aux_signal.valid
        ? make_centered_16_9_rect(aux_signal.center_x, aux_signal.center_y,
                                  aux_signal.width, aux_signal.height, aux_scale)
        : impl_->configured_roi("cam_02", "default_focus", aux_signal.width, aux_signal.height);
    aux_region.source_type = aux_signal.ball_confidence > 0.40
        ? FocusRegionSource::BALL_DETECTION
        : (aux_signal.activity > 0.20 ? FocusRegionSource::MOTION_CLUSTER : FocusRegionSource::DEFAULT);
    aux_region.confidence = clamp01(std::max(aux_signal.ball_confidence,
                                            aux_signal.goal_activity * 0.80 + 0.08));

    result.regions.push_back(main_region);
    result.regions.push_back(aux_region);

    {
        auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            it->second.last_focus_region_timestamp_ms = result.timestamp_ms;
        }
    }

    return result;
}

// ============================================================================
// 多机位决策
// ============================================================================

ProgramDecision VisionService::generate_program_decision(const std::string& match_id) {
    // Camera choice is based on the latest per-camera signals and follows the
    // frozen reason enum consumed by record-program-module.
    ProgramDecision decision;
    decision.match_id = match_id;

    Impl::FrameSignal main_signal = impl_->default_signal("cam_01");
    Impl::FrameSignal aux_signal = impl_->default_signal("cam_02");
    {
        const auto it = impl_->matches.find(match_id);
        if (it != impl_->matches.end()) {
            main_signal = impl_->latest_or_default(it->second, "cam_01", true);
            aux_signal = impl_->latest_or_default(it->second, "cam_02", true);
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
            it->second.last_decision_timestamp_ms = decision.timestamp_ms;
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

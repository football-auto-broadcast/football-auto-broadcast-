#pragma once
#include <string>
#include <vector>
#include <json.hpp>

using json = nlohmann::json;

// ============================================================
// 1. Error Codes (Contract Section 7.4)
// ============================================================
namespace ErrorCode {
    constexpr int SUCCESS                    = 0;
    constexpr int PARAM_ERROR               = 1001;
    constexpr int RESOURCE_NOT_INITIALIZED  = 1002;
    constexpr int INPUT_SOURCE_UNAVAILABLE  = 1003;
    constexpr int VIDEO_STREAM_ERROR        = 1004;
    constexpr int FILE_WRITE_FAILED         = 1005;
    constexpr int FILE_READ_FAILED          = 1006;
    constexpr int TASK_EXECUTION_FAILED     = 1007;
    constexpr int UPSTREAM_UNREACHABLE      = 1008;
    constexpr int RESOURCE_NOT_FOUND        = 1009;
    constexpr int STATE_CONFLICT            = 1010;
    constexpr int CONFIG_ERROR              = 1011;
    constexpr int CAM_SDK_INIT_FAILED       = 1012;
    constexpr int CAM_ENUM_FAILED           = 1013;
    constexpr int CAM_STREAM_FAILED         = 1014;
    constexpr int CAM_NOT_BOUND             = 1015;
    constexpr int CAM_SERIAL_CONFLICT       = 1016;
    constexpr int FOCUS_REGION_EXPIRED      = 1017;
    constexpr int DECISION_TIMEOUT          = 1018;
    constexpr int DISK_SPACE_INSUFFICIENT   = 1019;
}

inline const char* error_message(int code) {
    switch (code) {
        case ErrorCode::SUCCESS:                   return "ok";
        case ErrorCode::PARAM_ERROR:               return "Param Error";
        case ErrorCode::RESOURCE_NOT_INITIALIZED:  return "Resource Not Initialized";
        case ErrorCode::INPUT_SOURCE_UNAVAILABLE:  return "Input Source Unavailable";
        case ErrorCode::VIDEO_STREAM_ERROR:        return "Video Stream Error";
        case ErrorCode::FILE_WRITE_FAILED:         return "File Write Failed";
        case ErrorCode::FILE_READ_FAILED:          return "File Read Failed";
        case ErrorCode::TASK_EXECUTION_FAILED:     return "Task Execution Failed";
        case ErrorCode::UPSTREAM_UNREACHABLE:      return "Upstream Service Unreachable";
        case ErrorCode::RESOURCE_NOT_FOUND:        return "Resource Not Found";
        case ErrorCode::STATE_CONFLICT:            return "State Conflict";
        case ErrorCode::CONFIG_ERROR:              return "Config Error";
        case ErrorCode::CAM_SDK_INIT_FAILED:       return "Camera SDK Init Failed";
        case ErrorCode::CAM_ENUM_FAILED:           return "Camera Enumeration Failed";
        case ErrorCode::CAM_STREAM_FAILED:         return "Camera Stream Failed";
        case ErrorCode::CAM_NOT_BOUND:             return "Camera Not Bound";
        case ErrorCode::CAM_SERIAL_CONFLICT:       return "Camera Serial Conflict";
        case ErrorCode::FOCUS_REGION_EXPIRED:      return "Focus Region Expired";
        case ErrorCode::DECISION_TIMEOUT:          return "Decision Timeout";
        case ErrorCode::DISK_SPACE_INSUFFICIENT:   return "Disk Space Insufficient";
        default: return "Unknown Error";
    }
}

// ============================================================
// 2. Match State Enum (Contract Section 7.5)
// ============================================================
namespace MatchState {
    constexpr const char* IDLE          = "idle";
    constexpr const char* INITIALIZING  = "initializing";
    constexpr const char* RUNNING       = "running";
    constexpr const char* RECORDING     = "recording";
    constexpr const char* PROCESSING    = "processing";
    constexpr const char* SUCCESS       = "success";
    constexpr const char* FAILED        = "failed";
    constexpr const char* STOPPED       = "stopped";
    constexpr const char* DEGRADED      = "degraded";
}

// Helper: build standard response
inline json make_response(int code, json data = nullptr) {
    json resp;
    resp["code"] = code;
    resp["message"] = error_message(code);
    if (!data.is_null()) resp["data"] = data;
    else resp["data"] = json::object();
    return resp;
}

// ============================================================
// 3. API Request/Response Models (Contract Section 8)
// ============================================================

// --- 8.1 E -> A: Ingest Init ---
struct CameraEntry {
    std::string camera_id;    // "cam_01" / "cam_02"
    std::string role;         // "main" / "aux"
    std::string model;        // "MV-CE050-30GC"
    std::string lens;         // "6mm_C_mount"
    std::string stream_uri;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraEntry, camera_id, role, model, lens, stream_uri)

struct NetworkConfig {
    std::string mode = "static_ip";    // "static_ip"
    std::string subnet = "192.168.10.0/24";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NetworkConfig, mode, subnet)

struct CaptureConfig {
    std::string internal_source_resolution = "5mp_native";
    std::string rtsp_output_resolution = "1920x1080";
    int fps = 25;
    std::string pixel_format = "bgr8_or_nv12";
    std::string video_codec = "h264";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CaptureConfig, internal_source_resolution, rtsp_output_resolution, fps, pixel_format, video_codec)

struct CameraParamStrategy {
    std::string load_from = "mvs_user_set_or_config";
    std::string trigger_mode = "continuous";
    bool allow_runtime_page_edit = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraParamStrategy, load_from, trigger_mode, allow_runtime_page_edit)

struct IngestInitRequest {
    std::string match_id;
    std::vector<CameraEntry> cameras;
    NetworkConfig network_config;
    CaptureConfig capture_config;
    CameraParamStrategy camera_param_strategy;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(IngestInitRequest, match_id, cameras, network_config, capture_config, camera_param_strategy)

// --- 8.2 E -> B: Record Init ---
struct InputStreamEntry {
    std::string camera_id;
    std::string role;
    std::string stream_uri;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(InputStreamEntry, camera_id, role, stream_uri)

struct StorageConfig {
    std::string raw_root;
    std::string program_root;
    std::string metadata_root;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StorageConfig, raw_root, program_root, metadata_root)

struct ProgramConfig {
    std::string output_resolution = "1920x1080";
    int fps = 25;
    std::string default_mode = "follow_multi_focus_regions";
    bool enable_dual_camera_cut = true;
    std::string aspect_ratio = "16:9";
    std::string crop_policy = "expand_then_clip";
    int min_camera_hold_ms = 2000;
    std::string smoothing_level = "light_anti_shake";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProgramConfig, output_resolution, fps, default_mode, enable_dual_camera_cut, aspect_ratio, crop_policy, min_camera_hold_ms, smoothing_level)

struct RecordSubConfig {
    std::string container = "mp4";
    std::string video_codec = "h264";
    bool save_raw_recordings = true;
    bool save_cut_recordings = true;
    bool save_program_recording = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RecordSubConfig, container, video_codec, save_raw_recordings, save_cut_recordings, save_program_recording)

struct RecordInitRequest {
    std::string match_id;
    std::vector<InputStreamEntry> input_streams;
    StorageConfig storage_config;
    ProgramConfig program_config;
    RecordSubConfig record_config;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RecordInitRequest, match_id, input_streams, storage_config, program_config, record_config)

// --- 8.4 E -> C: Vision Init ---
struct StreamEntry {
    std::string camera_id;
    std::string role;
    std::string stream_uri;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StreamEntry, camera_id, role, stream_uri)

struct EventConfig {
    bool enable_goal_candidate = true;
    bool enable_shot_candidate = true;
    bool enable_danger_attack_candidate = true;
    bool enable_celebration_candidate = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(EventConfig, enable_goal_candidate, enable_shot_candidate, enable_danger_attack_candidate, enable_celebration_candidate)

struct FusionConfig {
    bool enable_dual_camera_focus_regions = true;
    bool enable_program_decision = true;
    int focus_region_update_ms = 200;
    std::string aux_camera_role = "corner_goal_side";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FusionConfig, enable_dual_camera_focus_regions, enable_program_decision, focus_region_update_ms, aux_camera_role)

struct DefaultRegionPolicy {
    std::string cam_01 = "half_field_center_safe_16_9";
    std::string cam_02 = "goal_side_attack_area_16_9";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DefaultRegionPolicy, cam_01, cam_02)

struct VisionInitRequest {
    std::string match_id;
    std::vector<StreamEntry> streams;
    EventConfig event_config;
    FusionConfig fusion_config;
    DefaultRegionPolicy default_region_policy;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VisionInitRequest, match_id, streams, event_config, fusion_config, default_region_policy)

// --- 8.6 E -> D: Highlight Generate ---
struct ClipPolicyEntry {
    int pre_sec;
    int post_sec;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ClipPolicyEntry, pre_sec, post_sec)

struct ClipPolicy {
    ClipPolicyEntry goal_candidate{8, 10};
    ClipPolicyEntry shot_candidate{6, 6};
    ClipPolicyEntry danger_attack_candidate{5, 5};
    ClipPolicyEntry celebration_candidate{3, 8};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ClipPolicy, goal_candidate, shot_candidate, danger_attack_candidate, celebration_candidate)

struct HighlightGenerateRequest {
    std::string match_id;
    std::string mode = "full_highlight";
    std::string record_index_path;
    std::string event_candidates_path;
    ClipPolicy clip_policy;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(HighlightGenerateRequest, match_id, mode, record_index_path, event_candidates_path, clip_policy)

// ============================================================
// 4. Match Object
// ============================================================
struct Match {
    std::string match_id;
    std::string match_name;
    std::string status = "idle";
    int64_t created_at = 0;
    int64_t started_at = 0;
    int64_t finished_at = 0;

    // Config embedded in match for start orchestration
    std::string data_root;
    std::vector<CameraEntry> cameras;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Match,
    match_id, match_name, status,
    created_at, started_at, finished_at,
    data_root, cameras
)

// ============================================================
// 5. Module Status (per-module, Contract Section 9.2/9.3)
// ============================================================
struct ModuleStatus {
    std::string module_name;
    std::string status = "unreachable";   // idle/initializing/running/recording/processing/success/failed/stopped/degraded
    std::string last_error;
    int64_t last_checked = 0;

    // A-module fields
    std::string cam_01_status = "unknown";
    std::string cam_02_status = "unknown";
    double cam_01_fps = 0.0;
    double cam_02_fps = 0.0;

    // B-module fields
    std::string cam_01_cut_status = "unknown";
    std::string cam_02_cut_status = "unknown";
    std::string current_program_camera_id = "unknown";
    std::string program_output_status = "unknown";
    double record_duration_sec = 0.0;
    double disk_free_gb = 0.0;
    std::string last_warning;

    // C-module fields
    bool focus_region_cam_01_ready = false;
    bool focus_region_cam_02_ready = false;
    std::string last_program_decision_camera = "unknown";
    int64_t last_focus_region_timestamp_ms = 0;
    int64_t last_decision_timestamp_ms = 0;

    // D-module fields
    std::string last_task_status = "unknown";
    std::string last_result_path;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ModuleStatus,
    module_name, status, last_error, last_checked,
    cam_01_status, cam_02_status, cam_01_fps, cam_02_fps,
    cam_01_cut_status, cam_02_cut_status,
    current_program_camera_id, program_output_status,
    record_duration_sec, disk_free_gb, last_warning,
    focus_region_cam_01_ready, focus_region_cam_02_ready,
    last_program_decision_camera,
    last_focus_region_timestamp_ms, last_decision_timestamp_ms,
    last_task_status, last_result_path
)

#ifndef INGEST_STREAMING_STREAM_CONFIG_H_
#define INGEST_STREAMING_STREAM_CONFIG_H_

#include <string>
#include <cstdint>

namespace ingest {

// ============================================================================
// 以下所有结构体字段名与 Team Interface Contract v1.2 Frozen §8.1
// "E→A 初始化采集任务" 请求体 1:1 对应，禁止单方面改名或改语义
// ============================================================================

/// @brief 单路相机绑定配置 — contract §8.1 cameras[]
struct CameraBinding {
    std::string camera_id;            ///< "cam_01" | "cam_02"
    std::string role;                 ///< "main" | "aux"
    std::string model;                ///< "MV-CE050-30GC"
    std::string lens;                 ///< "6mm_C_mount"
    std::string serial_number;        ///< 相机序列号（SDK 枚举匹配依据）
    std::string stream_uri;           ///< 输出 RTSP URI（A 模块内部使用）
};

/// @brief 网络配置 — contract §8.1 network_config
struct NetworkConfig {
    std::string mode;                 ///< "static_ip"
    std::string subnet;               ///< e.g. "192.168.10.0/24"
};

/// @brief 采集参数 — contract §8.1 capture_config
struct CaptureConfig {
    std::string internal_source_resolution;  ///< "5mp_native"
    std::string rtsp_output_resolution;      ///< "1920x1080"
    uint32_t    fps = 25;                    ///< 帧率
    std::string pixel_format;                ///< "bgr8_or_nv12"
    std::string video_codec;                 ///< "h264"
};

/// @brief 相机参数加载策略 — contract §8.1 camera_param_strategy
struct CameraParamStrategy {
    std::string load_from;                   ///< "mvs_user_set_or_config"
    std::string trigger_mode;                ///< "continuous"
    bool        allow_runtime_page_edit = false;
};

/// @brief E→A 初始化请求完整结构 — contract §8.1 请求体
struct IngestInitRequest {
    std::string            match_id;
    CameraBinding          cameras[2];       ///< 索引 0 = cam_01, 索引 1 = cam_02
    NetworkConfig          network_config;
    CaptureConfig          capture_config;
    CameraParamStrategy    camera_param_strategy;
};

} // namespace ingest

#endif // INGEST_STREAMING_STREAM_CONFIG_H_

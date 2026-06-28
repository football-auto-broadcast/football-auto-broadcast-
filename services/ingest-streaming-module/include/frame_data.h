#ifndef INGEST_STREAMING_FRAME_DATA_H_
#define INGEST_STREAMING_FRAME_DATA_H_

#include <cstdint>
#include <vector>
#include <string>

// 前向声明，避免强耦合海康 SDK 到所有消费端
// 实际编译时通过 camera_controller.cpp 中的 MvCameraControl.h 完成转换
namespace ingest {

/// @brief 标准化输出帧 — 从海康原始帧转换后的统一格式
///
/// 字段语义 100% 对齐 Team Interface Contract v1.2 Frozen：
///   §7.1 — snake_case 命名
///   §7.2 — timestamp_ms 为 Windows 主机系统时间戳(毫秒)，类型 integer
///
/// 本结构体是 A 模块对下游（B/C 模块）输出的帧数据载体。
/// 严禁在此结构体中自行发明像素格式语义或时间戳基准。
struct FrameData {
    // ===================================================================
    // 来源标识（contract §2.2 camera_id）
    // ===================================================================
    std::string camera_id;            ///< "cam_01" | "cam_02"

    // ===================================================================
    // 图像数据
    // ===================================================================
    std::vector<unsigned char> data;  ///< 像素数据（BGR8 逐行排列）
    uint32_t width  = 0;             ///< 输出宽，默认 1920
    uint32_t height = 0;             ///< 输出高，默认 1080
    uint32_t stride = 0;             ///< 行字节跨度（width * channels）
    uint32_t pixel_format = 0;       ///< 对应海康 SDK MvGvspPixelType 枚举值（透传）

    // ===================================================================
    // 时间戳（contract §7.2）
    // ===================================================================
    int64_t  timestamp_ms = 0;       ///< Windows 主机系统时间戳（毫秒）
    uint64_t frame_num    = 0;       ///< SDK 帧序号 nFrameNum

    // ===================================================================
    // 设备元数据（透传，供调试和降级决策）
    // ===================================================================
    int64_t  dev_timestamp = 0;      ///< 设备时间戳（合并 nDevTimeStampHigh/Low）
    uint32_t lost_packets  = 0;      ///< 本帧丢包数（nLostPacket）

    // ===================================================================
    // 质量标识
    // ===================================================================
    bool valid = false;              ///< 帧数据是否有效
};

} // namespace ingest

#endif // INGEST_STREAMING_FRAME_DATA_H_

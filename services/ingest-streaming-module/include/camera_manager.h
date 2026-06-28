#ifndef INGEST_STREAMING_CAMERA_MANAGER_H_
#define INGEST_STREAMING_CAMERA_MANAGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "camera_controller.h"
#include "stream_config.h"

namespace ingest {

// ============================================================================
// CameraManager — 模块 A 顶层编排类
// ============================================================================
///
/// 职责（contract §3.1 模块 A 责任边界）：
///   1. SDK 全局 Initialize / Finalize（进程级单次调用）
///   2. 枚举 GigE 设备 → 按序列号匹配 cam_01 / cam_02
///   3. 创建并持有两个独立的 CameraController 实例
///   4. 接收 E 模块的 HTTP 控制指令（init / start / stop）
///   5. 汇总双路状态，供 E 模块 HTTP GET /status 返回
///
/// 调用时序（严格）：
///   1. InitializeSDK()
///   2. Init(req)      ← E 发来 POST /api/v1/ingest/matches/init
///   3. StartAll()     ← E 发来 POST /api/v1/ingest/matches/start
///      ...（比赛进行中）...
///   4. StopAll()      ← E 发来 POST /api/v1/ingest/matches/stop
///   5. ShutdownAll()
///   6. FinalizeSDK()
///
/// @note  全局单实例（一个进程只跑一个 CameraManager）
class CameraManager {
public:
    CameraManager();
    ~CameraManager();

    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    // ===================================================================
    // SDK 全局生命周期
    // ===================================================================

    /// @brief 调用 MV_CC_Initialize()；整个进程生命周期调用一次
    int InitializeSDK();

    /// @brief 调用 MV_CC_Finalize()；进程退出前调用
    int FinalizeSDK();

    /// @brief SDK 是否已初始化
    bool IsSDKInitialized() const { return sdk_initialized_; }

    // ===================================================================
    // E 模块控制接口
    // ===================================================================

    /// @brief 接收 E 模块 init 请求 → 枚举 → 匹配 → 创建双相机控制器 → 打开设备
    /// @param req  E 发来的完整初始化请求（contract §8.1）
    /// @return 0 成功；非 0 为 contract §7.4 错误码
    int Init(const IngestInitRequest& req);

    /// @brief 双路同时开始取流（顺序启动，非严格同步）
    int StartAll();

    /// @brief 双路同时停止取流
    int StopAll();

    /// @brief 关闭双路设备 + 销毁句柄；通常在比赛结束后调用
    int ShutdownAll();

    // ===================================================================
    // 帧消费入口 — 供编码/RTSP 线程按 camera_id 获取帧
    // ===================================================================

    /// @brief 获取指定相机的控制器指针（只读访问）
    CameraController* GetController(const std::string& camera_id) const;

    /// @brief 便捷方法：从 cam_01 非阻塞取帧
    bool TryPopMainFrame(FrameData& out_frame);

    /// @brief 便捷方法：从 cam_02 非阻塞取帧
    bool TryPopAuxFrame(FrameData& out_frame);

    // ===================================================================
    // 全局状态汇总 — contract §9.2 状态页最小展示项
    // ===================================================================

    struct GlobalStatus {
        std::string                     match_id;
        std::string                     module_state;   ///< 遵循 contract §7.5
        CameraController::StatusSummary cam_01;
        CameraController::StatusSummary cam_02;
        std::string                     last_error;
    };
    GlobalStatus GetGlobalStatus() const;

    /// @brief 当前比赛 ID
    const std::string& CurrentMatchId() const { return match_id_; }

private:
    // ===================================================================
    // 设备枚举与匹配
    // ===================================================================

    /// @brief 枚举 GigE 设备列表，按序列号匹配两台目标相机
    /// @param serial_main  主机位序列号（e.g. "F92514845"）
    /// @param serial_aux   辅机位序列号（e.g. "D91363830"）
    /// @param out_main      [out] 匹配到的主机位设备信息（SDK 内部分配内存）
    /// @param out_aux       [out] 匹配到的辅机位设备信息
    /// @return MV_OK 双路均匹配成功；否则返回错误码
    /// @note   匹配策略：遍历枚举结果，逐项比较 chSerialNumber 字符串
    ///         禁止按枚举顺序隐式绑定（contract §2.2）
    int EnumerateAndMatch(const std::string& serial_main,
                          const std::string& serial_aux,
                          MV_CC_DEVICE_INFO*& out_main,
                          MV_CC_DEVICE_INFO*& out_aux);

    // ===================================================================
    // 成员变量
    // ===================================================================

    bool sdk_initialized_ = false;

    std::unique_ptr<CameraController> cam_01_;
    std::unique_ptr<CameraController> cam_02_;

    std::string match_id_;

    mutable std::mutex state_mutex_;
    std::string        last_error_;
};

} // namespace ingest

#endif // INGEST_STREAMING_CAMERA_MANAGER_H_

#ifndef INGEST_STREAMING_CAMERA_CONTROLLER_H_
#define INGEST_STREAMING_CAMERA_CONTROLLER_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "MvCameraControl.h"
#include "frame_data.h"

namespace ingest {

// ============================================================================
// 相机状态枚举 — 遵循 contract §7.5 统一状态值
// ============================================================================
enum class CameraState {
    Idle,
    Initializing,
    Running,
    Degraded,   ///< 断流重连中（contract §9.4: 单路断流 5s 内为此状态）
    Stopped,
    Failed
};

/// @brief 将 CameraState 转为 contract §7.5 约定的 snake_case 字符串
const char* CameraStateToString(CameraState s);

// ============================================================================
// CameraController — 单台 GigE 工业相机全生命周期控制器
// ============================================================================
///
/// 职责边界（contract §3.1）：
///   1. 封装海康 MVS SDK 的 C 接口：CreateHandle → OpenDevice → 取流 → CloseDevice
///   2. 通过 SDK 回调接收原始帧，内部转换为标准 FrameData
///   3. 维护线程安全的帧缓冲队列（SDK 回调线程 --> 编码消费线程）
///   4. 断流检测、自动重连、降级状态上报
///
/// 线程模型：
///   ┌─────────────────────┐     ┌──────────────────────┐
///   │ SDK 内部取流线程     │ ──► │ OnFrameCallback      │  静态 C 回调
///   │ (海康驱动层)         │     │ → ProcessFrame()     │  实例方法
///   └─────────────────────┘     │ → 入队 frame_queue_  │  mutex + cv
///                               └──────────┬───────────┘
///                                          │
///   ┌─────────────────────┐                │
///   │ 编码 / RTSP 消费线程 │ ◄──────────────┘
///   │ TryPopFrame()       │  mutex + cv 出队
///   └─────────────────────┘
///
/// @note  每个 CameraController 实例绑定且仅绑定一台物理相机
/// @note  禁止拷贝 — 每台相机对应唯一的 SDK handle
class CameraController {
public:
    /// @brief 构造时仅绑定逻辑标识，不执行任何 SDK 操作
    /// @param camera_id      "cam_01" 或 "cam_02"
    /// @param target_serial  相机序列号（e.g. "F92514845"），用于枚举匹配
    CameraController(std::string camera_id, std::string target_serial);
    ~CameraController();

    // ---- 禁止拷贝 ----
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    // ---- 允许移动 ----
    CameraController(CameraController&&) = default;
    CameraController& operator=(CameraController&&) = default;

    // ===================================================================
    // 生命周期 API（调用时序必须严格遵守）
    // ===================================================================

    /// @brief 基于已枚举到的设备信息，创建 SDK 句柄
    /// @param pDevInfo  从 MV_CC_EnumDevices() 结果中匹配到的设备指针
    /// @return MV_OK (0) 成功；其他值对应 contract §7.4 错误码
    /// @pre    MV_CC_Initialize() 已被调用
    int Initialize(void* pDevInfo);

    /// @brief 打开设备连接（MV_CC_OpenDevice）
    /// @return MV_OK 成功
    /// @pre    Initialize() 已成功
    int Open();

    /// @brief 注册取流回调 + 开始连续取流
    /// 内部依次执行：
    ///   1. 设置 TriggerMode = Off（连续采集）
    ///   2. 探测并应用 OptimalPacketSize（仅 GigE）
    ///   3. 注册 ImageCallBackEx2
    ///   4. MV_CC_StartGrabbing
    /// @return MV_OK 成功
    /// @pre    Open() 已成功
    int StartGrabbing();

    /// @brief 停止取流 + 注销回调
    /// @return MV_OK 成功
    int StopGrabbing();

    /// @brief 关闭设备 + 销毁句柄，释放 SDK 资源
    /// @return MV_OK 成功
    int Close();

    // ===================================================================
    // 帧消费接口 — 供编码/RTSP 线程调用
    // ===================================================================

    /// @brief 非阻塞取帧；队列空则立即返回 false
    /// @param out_frame  [out] 出队帧
    /// @return true 取到帧，false 队列为空
    bool TryPopFrame(FrameData& out_frame);

    /// @brief 带超时的阻塞取帧
    /// @param out_frame   [out] 出队帧
    /// @param timeout_ms  最大等待毫秒数
    /// @return true 取到帧，false 超时
    bool WaitPopFrame(FrameData& out_frame, uint32_t timeout_ms);

    /// @brief 丢弃队列中所有帧（编码线程追赶实时时使用）
    void DrainFrames();

    /// @brief 获取当前队列中积压的帧数
    size_t QueueDepth() const;

    // ===================================================================
    // 状态查询 — 供 HTTP status 接口和 CameraManager 汇总使用
    // ===================================================================

    CameraState State()          const { return state_.load(std::memory_order_acquire); }
    bool        IsConnected()    const;
    uint64_t    TotalFrameCount() const { return total_frames_.load(std::memory_order_relaxed); }
    uint32_t    TotalLostPackets()const { return total_lost_.load(std::memory_order_relaxed); }
    double      CurrentFps()     const;
    std::string LastError()      const;

    const std::string& CameraId()       const { return camera_id_; }
    const std::string& TargetSerial()   const { return target_serial_; }

    /// @brief 结构化状态摘要（contract §9.2 状态页最小展示项）
    struct StatusSummary {
        std::string camera_id;
        std::string state;            ///< idle / initializing / running / degraded / stopped / failed
        bool        connected = false;
        uint64_t    frame_count = 0;
        double      fps = 0.0;
        uint32_t    lost_packets = 0;
        size_t      queue_depth = 0;
        std::string stream_uri;
        bool        streaming_active = false;
        int         output_width = 0;
        int         output_height = 0;
        int         output_fps = 0;
        int64_t     last_frame_timestamp_ms = 0;
        uint64_t    dropped_frames = 0;
        uint64_t    encoded_frames = 0;
        std::string last_error;
    };
    StatusSummary GetStatusSummary() const;

    // ===================================================================
    // 相机参数控制
    // ===================================================================

    int SetContinuousMode();                ///< TriggerMode = Off
    int SetOptimalPacketSize();             ///< GevSCPSPacketSize 最优值
    int SetIntValue(const char* key, int64_t value);
    int SetEnumValue(const char* key, unsigned int value);
    int SetFloatValue(const char* key, float value);

    /// @brief 设置 RTSP 推流目标 URI（MediaMTX 发布地址）
    /// @param uri  例如 "rtsp://127.0.0.1:8554/main"
    void SetRtspUri(const std::string& uri);

    /// @brief 设置输出分辨率与帧率（用于 VideoWriter 初始化）
    void SetOutputParams(int width, int height, int fps);

private:
    // ===================================================================
    // SDK 回调（C → C++ 转发）
    // ===================================================================

    /// @brief 静态 C 回调，由 SDK 取流线程调用
    /// @note  pUser 指向 CameraController 实例的 this 指针
    static void __stdcall OnFrameCallback(MV_FRAME_OUT* pstFrame,
                                          void* pUser, bool bAutoFree);

    /// @brief 实例级帧处理：SDK 原始帧 → FrameData → 入队列
    void ProcessFrame(MV_FRAME_OUT* pstFrame);

    /// @brief SDK 原始帧 → 标准化 FrameData
    static FrameData ConvertFrame(const MV_FRAME_OUT* pstFrame,
                                  const std::string& camera_id);

    // ===================================================================
    // 断流检测与重连（contract §9.4）
    // ===================================================================

    /// @brief 注册流异常回调（MvStreamExceptionCallback）
    void RegisterStreamExceptionCallback();

    /// @brief SDK 流异常回调的静态转发
    static void __stdcall OnStreamException(
        MV_CC_STREAM_EXCEPTION_INFO* pstExceptionInfo, void* pUser);

    /// @brief 断流重连：5s 内恢复 → Degraded→Running；超时 → Failed
    void AttemptReconnect();

    /// @brief 记录最近错误（线程安全）
    void SetLastError(const std::string& msg);

    // ===================================================================
    // 推流线程（Milestone 3）
    // ===================================================================

    /// @brief 推流线程主循环：WaitPopFrame → cv::Mat → VideoWriter.write
    /// 遇到 valid=false 毒药帧时释放编码器并退出
    void StreamingLoop();

    // ===================================================================
    // 成员变量
    // ===================================================================

    // ---- 逻辑标识 ----
    const std::string camera_id_;
    const std::string target_serial_;

    // ---- SDK 资源 ----
    void*       handle_    = nullptr;  ///< MV_CC_CreateHandle 返回的设备句柄
    const MV_CC_DEVICE_INFO* pDevInfo_ = nullptr;  ///< 枚举时匹配到的设备信息
    bool        grabbing_  = false;   ///< 是否正在取流

    // ===================================================================
    // 帧缓冲队列 — 线程安全
    // ===================================================================
    static constexpr size_t kMaxQueueSize = 8;   ///< 最大缓冲帧数（~320ms@25fps）
    std::queue<FrameData>   frame_queue_;
    mutable std::mutex      queue_mutex_;
    std::condition_variable queue_cv_;

    // ===================================================================
    // 状态与统计
    // ===================================================================
    std::atomic<CameraState> state_{CameraState::Idle};
    std::atomic<uint64_t>    total_frames_{0};
    std::atomic<uint32_t>    total_lost_{0};
    std::atomic<uint64_t>    dropped_frames_{0};
    std::atomic<uint64_t>    encoded_frames_{0};
    std::atomic<int64_t>     last_frame_timestamp_ms_{0};

    // ---- 帧率滑动窗口 ----
    mutable std::mutex               fps_mutex_;
    std::chrono::steady_clock::time_point fps_window_start_;
    uint64_t                         fps_frame_count_in_window_ = 0;
    double                           last_fps_ = 0.0;

    // ---- 错误 ----
    mutable std::mutex error_mutex_;
    std::string        last_error_;

    // ---- 重连 ----
    std::atomic<bool> reconnect_in_progress_{false};

    // ---- RTSP 推流（Milestone 3）----
    std::string           rtsp_uri_;             ///< 推流目标，如 rtsp://127.0.0.1:8554/main
    int                   output_width_  = 1920;
    int                   output_height_ = 1080;
    int                   output_fps_    = 25;
    std::atomic<bool>     streaming_active_{false};
    std::thread           streaming_thread_;
};

} // namespace ingest

#endif // INGEST_STREAMING_CAMERA_CONTROLLER_H_

#include "camera_controller.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

// OpenCV — 用于帧缩放和格式转换
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

// spdlog 异步日志
#include "spdlog/spdlog.h"

// Windows 高精度时间戳
#ifdef _WIN32
#include <Windows.h>
#else
#include <chrono>
#endif

namespace ingest {

#ifndef INGEST_DEFAULT_FFMPEG_EXE_PATH
#define INGEST_DEFAULT_FFMPEG_EXE_PATH "ffmpeg.exe"
#endif

// ============================================================================
// 工具函数
// ============================================================================

const char* CameraStateToString(CameraState s) {
    switch (s) {
        case CameraState::Idle:          return "idle";
        case CameraState::Initializing:  return "initializing";
        case CameraState::Running:       return "running";
        case CameraState::Degraded:      return "degraded";
        case CameraState::Stopped:       return "stopped";
        case CameraState::Failed:        return "failed";
        default:                         return "unknown";
    }
}

/// @brief 获取当前 Windows 主机时间戳（毫秒）— contract §7.2 timestamp_ms 唯一来源
static int64_t HostTimestampMs() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    // 1601-01-01 到 1970-01-01 的偏移：116444736000000000 百纳秒
    constexpr int64_t EPOCH_DIFF = 116444736000000000LL;
    ULARGE_INTEGER ul;
    ul.LowPart  = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>((ul.QuadPart - EPOCH_DIFF) / 10000LL);
#else
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch()).count();
#endif
}

/// @brief 从 SDK 错误码生成可读字符串
static std::string SdkErrorString(int nRet) {
    std::ostringstream oss;
    oss << "SDK error 0x" << std::hex << nRet;
    return oss.str();
}

static std::string GetFfmpegExePath() {
#ifdef _WIN32
    char* env_path = nullptr;
    size_t env_size = 0;
    if (_dupenv_s(&env_path, &env_size, "INGEST_FFMPEG_EXE_PATH") == 0 &&
        env_path != nullptr && env_path[0] != '\0') {
        std::string value(env_path);
        free(env_path);
        return value;
    }
    free(env_path);
#else
    const char* env_path = std::getenv("INGEST_FFMPEG_EXE_PATH");
    if (env_path != nullptr && env_path[0] != '\0') {
        return env_path;
    }
#endif
    return INGEST_DEFAULT_FFMPEG_EXE_PATH;
}

static bool LooksLikeFilePath(const std::string& value) {
    return value.find(':') != std::string::npos ||
           value.find('/') != std::string::npos ||
           value.find('\\') != std::string::npos;
}

static std::string QuoteCommandArg(const std::string& value) {
    if (value.find_first_of(" \t\r\n\"") == std::string::npos) {
        return value;
    }

    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('"');
    return quoted;
}

// ============================================================================
// 构造 / 析构
// ============================================================================

CameraController::CameraController(std::string camera_id, std::string target_serial)
    : camera_id_(std::move(camera_id))
    , target_serial_(std::move(target_serial))
    , handle_(nullptr)
    , pDevInfo_(nullptr)
    , grabbing_(false)
{
    spdlog::info("[{}] CameraController created (target serial: {})",
                 camera_id_, target_serial_);
}

CameraController::~CameraController() {
    if (grabbing_) {
        StopGrabbing();
    }
    if (handle_ != nullptr) {
        Close();
    }
    spdlog::info("[{}] CameraController destroyed", camera_id_);
}

// ============================================================================
// 生命周期：Initialize → Open → StartGrabbing
// ============================================================================

int CameraController::Initialize(void* pDevInfo) {
    if (pDevInfo == nullptr) {
        SetLastError("Initialize: pDevInfo is null");
        return MV_E_PARAMETER; // 参数错误
    }

    state_.store(CameraState::Initializing, std::memory_order_release);
    pDevInfo_ = static_cast<const MV_CC_DEVICE_INFO*>(pDevInfo);

    // 校验序列号一致性（防御性检查）
    const auto* pGigEInfo = &pDevInfo_->SpecialInfo.stGigEInfo;
    std::string actual_serial(
        reinterpret_cast<const char*>(pGigEInfo->chSerialNumber));
    if (actual_serial != target_serial_) {
        std::ostringstream err;
        err << "Serial mismatch! Expected: " << target_serial_
            << ", Actual: " << actual_serial;
        SetLastError(err.str());
        state_.store(CameraState::Failed, std::memory_order_release);
        return MV_E_PARAMETER;
    }

    // 创建 SDK 句柄
    int nRet = MV_CC_CreateHandle(&handle_, pDevInfo_);
    if (MV_OK != nRet) {
        SetLastError("MV_CC_CreateHandle failed: " + SdkErrorString(nRet));
        state_.store(CameraState::Failed, std::memory_order_release);
        return nRet;
    }

    spdlog::info("[{}] SDK handle created (serial: {})", camera_id_, actual_serial);
    return MV_OK;
}

int CameraController::Open() {
    if (handle_ == nullptr) {
        SetLastError("Open: handle is null (call Initialize first)");
        return MV_E_HANDLE;
    }

    int nRet = MV_CC_OpenDevice(handle_, MV_ACCESS_Exclusive, 0);
    if (MV_OK != nRet) {
        SetLastError("MV_CC_OpenDevice failed: " + SdkErrorString(nRet));
        state_.store(CameraState::Failed, std::memory_order_release);
        return nRet;
    }

    spdlog::info("[{}] Device opened", camera_id_);
    return MV_OK;
}

int CameraController::StartGrabbing() {
    if (handle_ == nullptr) {
        SetLastError("StartGrabbing: handle is null");
        return MV_E_HANDLE;
    }

    if (grabbing_) {
        spdlog::warn("[{}] StartGrabbing called while already grabbing", camera_id_);
        return MV_OK;
    }

    // ---- 1. 设置触发模式为 Off（连续采集）----
    int nRet = SetContinuousMode();
    if (MV_OK != nRet) {
        SetLastError("SetContinuousMode failed: " + SdkErrorString(nRet));
        return nRet;
    }

    // ---- 2. 设置像素格式为 BGR8（FFmpeg rawvideo 输入要求）----
    // MV-CE050-30GC 默认输出 BayerRG8，需要显式切换到 BGR8
    nRet = MV_CC_SetEnumValue(handle_, "PixelFormat",
                               PixelType_Gvsp_BGR8_Packed);
    if (MV_OK != nRet) {
        SetLastError("Set PixelFormat to BGR8 failed: " + SdkErrorString(nRet));
        return nRet;
    }
    spdlog::info("[{}] PixelFormat set to BGR8", camera_id_);

    // ---- 3. 设置采集分辨率（缩放到 1920x1080，由 SDK 内部 ISP 完成）----
    nRet = MV_CC_SetIntValueEx(handle_, "Width", output_width_);
    if (MV_OK != nRet) {
        SetLastError("Set Width failed: " + SdkErrorString(nRet));
        return nRet;
    }

    nRet = MV_CC_SetIntValueEx(handle_, "Height", output_height_);
    if (MV_OK != nRet) {
        SetLastError("Set Height failed: " + SdkErrorString(nRet));
        return nRet;
    }
    spdlog::info("[{}] Resolution set to {}x{}",
                 camera_id_, output_width_, output_height_);

    // ---- 4. 探测并设置最优包大小（仅 GigE 有效）----
    if (pDevInfo_ != nullptr &&
        (pDevInfo_->nTLayerType == MV_GIGE_DEVICE ||
         pDevInfo_->nTLayerType == MV_GENTL_GIGE_DEVICE)) {
        nRet = SetOptimalPacketSize();
        if (MV_OK != nRet) {
            spdlog::warn("[{}] SetOptimalPacketSize failed (non-fatal): 0x{:x}",
                         camera_id_, nRet);
        }
    }

    // ---- 5. 注册取流回调（使用 Ex2 版本 + 手动释放）----
    // bAutoFree = false：SDK 不自动释放图像缓存，由回调内部调用 FreeImageBuffer
    nRet = MV_CC_RegisterImageCallBackEx2(handle_, OnFrameCallback,
                                          this, false);
    if (MV_OK != nRet) {
        SetLastError("RegisterImageCallBackEx2 failed: " + SdkErrorString(nRet));
        return nRet;
    }

    // ---- 6. 开始取流 ----
    nRet = MV_CC_StartGrabbing(handle_);
    if (MV_OK != nRet) {
        SetLastError("MV_CC_StartGrabbing failed: " + SdkErrorString(nRet));
        // 回滚：注销回调
        MV_CC_RegisterImageCallBackEx2(handle_, nullptr, nullptr, false);
        return nRet;
    }

    grabbing_ = true;

    // ---- 7. 启动 RTSP 推流线程（Milestone 3）----
    if (!rtsp_uri_.empty()) {
        streaming_active_.store(true, std::memory_order_release);
        streaming_thread_ = std::thread(&CameraController::StreamingLoop, this);
        spdlog::info("[{}] Streaming thread spawned -> {}", camera_id_, rtsp_uri_);
    } else {
        spdlog::warn("[{}] RTSP URI not set — streaming thread NOT started", camera_id_);
    }

    state_.store(CameraState::Running, std::memory_order_release);
    spdlog::info("[{}] Grabbing started", camera_id_);
    return MV_OK;
}

// ============================================================================
// 生命周期：StopGrabbing → Close
// ============================================================================

int CameraController::StopGrabbing() {
    if (!grabbing_) {
        return MV_OK;
    }

    // ---- 1. 通知推流线程退出 ----
    streaming_active_.store(false, std::memory_order_release);

    // ---- 2. 停止 SDK 取流 ----
    int nRet = MV_CC_StopGrabbing(handle_);
    if (MV_OK != nRet) {
        spdlog::warn("[{}] MV_CC_StopGrabbing returned: 0x{:x}",
                     camera_id_, nRet);
    }

    // 注销回调（传入 nullptr 即为注销）
    MV_CC_RegisterImageCallBackEx2(handle_, nullptr, nullptr, false);

    grabbing_ = false;

    // ---- 3. 推毒药帧 —— 唤醒推流线程退出 ----
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // 向队列推一个无效帧作为停止信号
        FrameData stop_signal;
        stop_signal.camera_id = camera_id_;
        stop_signal.valid = false;
        // 清空旧帧，只留停止信号
        while (!frame_queue_.empty()) frame_queue_.pop();
        frame_queue_.push(std::move(stop_signal));
    }
    queue_cv_.notify_all();

    // ---- 4. 等待推流线程退出（防止僵尸线程）----
    if (streaming_thread_.joinable()) {
        spdlog::info("[{}] Waiting for streaming thread to exit...", camera_id_);
        streaming_thread_.join();
        spdlog::info("[{}] Streaming thread joined", camera_id_);
    }

    state_.store(CameraState::Stopped, std::memory_order_release);
    spdlog::info("[{}] Grabbing stopped (total frames: {})",
                 camera_id_, total_frames_.load());
    return MV_OK;
}

int CameraController::Close() {
    if (handle_ == nullptr) {
        return MV_OK;
    }

    int nRet = MV_CC_CloseDevice(handle_);
    if (MV_OK != nRet) {
        spdlog::warn("[{}] MV_CC_CloseDevice returned: 0x{:x}",
                     camera_id_, nRet);
    }

    nRet = MV_CC_DestroyHandle(handle_);
    if (MV_OK != nRet) {
        spdlog::warn("[{}] MV_CC_DestroyHandle returned: 0x{:x}",
                     camera_id_, nRet);
    }

    handle_ = nullptr;
    pDevInfo_ = nullptr;
    spdlog::info("[{}] Device closed", camera_id_);
    return MV_OK;
}

// ============================================================================
// 帧缓冲队列 — SDK 回调线程 → 编码消费线程 的安全桥梁
// ============================================================================

bool CameraController::TryPopFrame(FrameData& out_frame) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (frame_queue_.empty()) {
        return false;
    }
    out_frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return out_frame.valid;  // 仅有效帧返回 true
}

bool CameraController::WaitPopFrame(FrameData& out_frame, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    bool success = queue_cv_.wait_for(lock,
        std::chrono::milliseconds(timeout_ms),
        [this] { return !frame_queue_.empty(); });

    if (!success) {
        return false;  // 超时
    }

    out_frame = std::move(frame_queue_.front());
    frame_queue_.pop();
    return out_frame.valid;
}

void CameraController::DrainFrames() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!frame_queue_.empty()) {
        frame_queue_.pop();
    }
    spdlog::debug("[{}] Frame queue drained", camera_id_);
}

size_t CameraController::QueueDepth() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return frame_queue_.size();
}

// ============================================================================
// SDK 回调链（静态 C 回调 → 实例方法）
// ============================================================================

void __stdcall CameraController::OnFrameCallback(
    MV_FRAME_OUT* pstFrame, void* pUser, bool bAutoFree)
{
    if (pUser == nullptr || pstFrame == nullptr) {
        // pUser 为空说明回调已被注销，安全忽略
        return;
    }

    auto* self = static_cast<CameraController*>(pUser);

    // 快速路径：pstFrame->pBufAddr 为空说明取流异常
    if (pstFrame->pBufAddr == nullptr) {
        spdlog::warn("[{}] Null buffer in callback", self->camera_id_);
        return;
    }

    self->ProcessFrame(pstFrame);

    // 手动释放图像缓存（因为我们注册时 bAutoFree=false）
    // MV_CC_FreeImageBuffer 的第一个参数必须是设备句柄 handle_，而非 pUser
    if (!bAutoFree && self->handle_ != nullptr) {
        MV_CC_FreeImageBuffer(self->handle_, pstFrame);
    }
}

void CameraController::ProcessFrame(MV_FRAME_OUT* pstFrame) {
    FrameData frame = ConvertFrame(pstFrame, camera_id_);
    const int64_t frame_timestamp_ms = frame.timestamp_ms;

    // ---- 入队（线程安全 + 背压控制）----
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        // 队列满时丢弃最旧帧（追赶实时策略，宁可丢旧帧也不阻塞 SDK 回调线程）
        if (frame_queue_.size() >= kMaxQueueSize) {
        if (frame_queue_.size() >= kMaxQueueSize) {
            frame_queue_.pop();
            dropped_frames_.fetch_add(1, std::memory_order_relaxed);
            // 不做告警——在 25fps 下偶尔丢帧是正常背压行为
            // 持续丢帧会反映在 QueueDepth 和 current_fps 上，由上层监控发现
        }

        }

        frame_queue_.push(std::move(frame));
    }
    queue_cv_.notify_one();

    // ---- 更新统计 ----
    total_frames_.fetch_add(1, std::memory_order_relaxed);
    total_lost_.fetch_add(pstFrame->stFrameInfo.nLostPacket,
                          std::memory_order_relaxed);
    last_frame_timestamp_ms_.store(frame_timestamp_ms, std::memory_order_relaxed);

    // ---- 帧率滑动窗口（2 秒窗口）----
    {
        std::lock_guard<std::mutex> lock(fps_mutex_);
        auto now = std::chrono::steady_clock::now();
        if (fps_frame_count_in_window_ == 0) {
            fps_window_start_ = now;
        }
        fps_frame_count_in_window_++;

        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - fps_window_start_).count();
        if (elapsed_ms >= 2000) {  // 每 2 秒刷新一次帧率
            last_fps_ = fps_frame_count_in_window_ * 1000.0 / elapsed_ms;
            last_fps_ = fps_frame_count_in_window_ * 1000.0 / elapsed_ms;
            fps_frame_count_in_window_ = 0;
        }
    }
}

// ============================================================================
// SDK 原始帧 → 标准化 FrameData
// ============================================================================

FrameData CameraController::ConvertFrame(
    const MV_FRAME_OUT* pstFrame, const std::string& camera_id)
{
    FrameData fd;
    fd.camera_id = camera_id;
    fd.valid     = true;

    const auto& info = pstFrame->stFrameInfo;

    // ---- 图像尺寸 ----
    fd.width  = info.nExtendWidth  > 0 ? info.nExtendWidth  : info.nWidth;
    fd.height = info.nExtendHeight > 0 ? info.nExtendHeight : info.nHeight;
    fd.pixel_format = static_cast<uint32_t>(info.enPixelType);

    // ---- 行跨度（BGR8 = 3 bytes/pixel，其他格式按需扩展）----
    uint32_t channels = 3; // 默认 BGR8
    fd.stride = fd.width * channels;

    // ---- 拷贝像素数据（深拷贝，解除对 SDK 内部缓冲区的依赖）----
    uint64_t frame_len = info.nFrameLenEx > 0 ? info.nFrameLenEx : info.nFrameLen;
    if (frame_len > 0 && pstFrame->pBufAddr != nullptr) {
        fd.data.resize(static_cast<size_t>(frame_len));
        std::memcpy(fd.data.data(), pstFrame->pBufAddr,
                    static_cast<size_t>(frame_len));
    }

    // ---- 时间戳（contract §7.2）----
    fd.timestamp_ms = HostTimestampMs();
    fd.frame_num    = info.nFrameNum;

    // ---- 设备时间戳（合并高/低 32 位）----
    fd.dev_timestamp = (static_cast<int64_t>(info.nDevTimeStampHigh) << 32)
                     |  static_cast<int64_t>(info.nDevTimeStampLow);

    // ---- 丢包 ----
    fd.lost_packets = info.nLostPacket;

    return fd;
}

// ============================================================================
// 状态查询
// ============================================================================

bool CameraController::IsConnected() const {
    if (handle_ == nullptr) return false;
    return MV_CC_IsDeviceConnected(handle_);
}

double CameraController::CurrentFps() const {
    std::lock_guard<std::mutex> lock(fps_mutex_);
    return last_fps_;
}

std::string CameraController::LastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void CameraController::SetLastError(const std::string& msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = msg;
    spdlog::error("[{}] {}", camera_id_, msg);
}

CameraController::StatusSummary CameraController::GetStatusSummary() const {
    StatusSummary s;
    s.camera_id    = camera_id_;
    s.state        = CameraStateToString(state_.load(std::memory_order_acquire));
    s.connected    = IsConnected();
    s.frame_count  = total_frames_.load(std::memory_order_relaxed);
    s.fps          = CurrentFps();
    s.lost_packets = total_lost_.load(std::memory_order_relaxed);
    s.queue_depth  = QueueDepth();
    s.stream_uri   = rtsp_uri_;
    s.streaming_active = streaming_active_.load(std::memory_order_acquire);
    s.output_width = output_width_;
    s.output_height = output_height_;
    s.output_fps = output_fps_;
    s.last_frame_timestamp_ms =
        last_frame_timestamp_ms_.load(std::memory_order_relaxed);
    s.dropped_frames = dropped_frames_.load(std::memory_order_relaxed);
    s.encoded_frames = encoded_frames_.load(std::memory_order_relaxed);
    s.last_error   = LastError();
    return s;
}

// ============================================================================
// 相机参数控制
// ============================================================================

int CameraController::SetContinuousMode() {
    return MV_CC_SetEnumValue(handle_, "TriggerMode", MV_TRIGGER_MODE_OFF);
}

int CameraController::SetOptimalPacketSize() {
    int nPacketSize = MV_CC_GetOptimalPacketSize(handle_);
    if (nPacketSize <= 0) {
        return nPacketSize;
    }
    return MV_CC_SetIntValueEx(handle_, "GevSCPSPacketSize", nPacketSize);
}

int CameraController::SetIntValue(const char* key, int64_t value) {
    return MV_CC_SetIntValueEx(handle_, key, value);
}

int CameraController::SetEnumValue(const char* key, unsigned int value) {
    return MV_CC_SetEnumValue(handle_, key, value);
}

int CameraController::SetFloatValue(const char* key, float value) {
    return MV_CC_SetFloatValue(handle_, key, value);
}

// ============================================================================
// 断流检测与自动重连（contract §9.4）
// ============================================================================

void __stdcall CameraController::OnStreamException(
    MV_CC_STREAM_EXCEPTION_INFO* pstExceptionInfo, void* pUser)
{
    if (pUser == nullptr || pstExceptionInfo == nullptr) return;

    auto* self = static_cast<CameraController*>(pUser);
    spdlog::warn("[{}] Stream exception detected (type: 0x{:x})",
                 self->camera_id_,
                 static_cast<unsigned int>(pstExceptionInfo->enExceptionType));

    // 标记为降级状态
    self->state_.store(CameraState::Degraded, std::memory_order_release);
}

void CameraController::AttemptReconnect() {
    // 防止并发重连
    bool expected = false;
    if (!reconnect_in_progress_.compare_exchange_strong(expected, true)) {
        return;  // 已有重连线程在执行
    }

    spdlog::info("[{}] Attempting reconnect...", camera_id_);
    state_.store(CameraState::Degraded, std::memory_order_release);

    // 5 秒内尝试重连（contract §9.4）
    auto start = std::chrono::steady_clock::now();
    bool recovered = false;

    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start).count() < 5)
    {
        if (IsConnected()) {
            recovered = true;
            break;
        }
        // 每次尝试间隔 500ms
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (recovered) {
        spdlog::info("[{}] Reconnect successful — back to Running", camera_id_);
        state_.store(CameraState::Running, std::memory_order_release);
    } else {
        spdlog::error("[{}] Reconnect failed after 5s — entering Failed state",
                      camera_id_);
        state_.store(CameraState::Failed, std::memory_order_release);
    }

    reconnect_in_progress_.store(false, std::memory_order_release);
}

// ============================================================================
// RTSP 推流（Milestone 3）
// ============================================================================

void CameraController::SetRtspUri(const std::string& uri) {
    rtsp_uri_ = uri;
    spdlog::info("[{}] RTSP URI set to: {}", camera_id_, uri);
}

void CameraController::SetOutputParams(int width, int height, int fps) {
    output_width_  = width;
    output_height_ = height;
    output_fps_    = fps;
}

void CameraController::StreamingLoop() {
    spdlog::info("[{}] StreamingLoop started — target: {}", camera_id_, rtsp_uri_);

    encoded_frames_.store(0, std::memory_order_relaxed);

    const std::string ffmpeg_exe = GetFfmpegExePath();
    if (LooksLikeFilePath(ffmpeg_exe) && !std::filesystem::exists(ffmpeg_exe)) {
        SetLastError("FFmpeg executable not found: " + ffmpeg_exe);
        streaming_active_.store(false, std::memory_order_release);
        return;
    }

    // ---- 构建 FFmpeg 命令行 ----
    // 使用外部 ffmpeg.exe 子进程：raw BGR 帧从 stdin 管道输入 →
    // libx264 编码（ultrafast + zerolatency 低延迟）→ 推流到 MediaMTX
    // 输出格式根据 URL 自动选择：rtmp:// → flv, rtsp:// → rtsp
    std::ostringstream cmd;
    cmd << QuoteCommandArg(ffmpeg_exe) << " -y -hide_banner -loglevel error"
        << " -f rawvideo"
        << " -pixel_format bgr24"
        << " -video_size " << output_width_ << "x" << output_height_
        << " -framerate " << output_fps_
        << " -i pipe:0"
        << " -c:v libx264"
        << " -preset ultrafast"
        << " -tune zerolatency"
        << " -pix_fmt yuv420p";

    // 根据 URI 协议选择输出格式
    if (rtsp_uri_.find("rtmp://") == 0) {
        cmd << " -f flv " << QuoteCommandArg(rtsp_uri_);
    } else {
        cmd << " -rtsp_transport tcp -f rtsp " << QuoteCommandArg(rtsp_uri_);
    }

    spdlog::info("[{}] Launching FFmpeg: {}", camera_id_, cmd.str());

    // ---- 启动 FFmpeg 子进程（_popen 打开管道写入端）----
#ifdef _WIN32
    FILE* ffmpeg_pipe = _popen(cmd.str().c_str(), "wb");
#else
    FILE* ffmpeg_pipe = popen(cmd.str().c_str(), "w");
#endif

    if (ffmpeg_pipe == nullptr) {
        SetLastError("Failed to launch FFmpeg process for " + rtsp_uri_);
        spdlog::error("[{}] FFmpeg launch failed — streaming aborted", camera_id_);
        streaming_active_.store(false, std::memory_order_release);
        return;
    }

    spdlog::info("[{}] FFmpeg subprocess started, feeding frames...", camera_id_);

    // ---- 主循环：消费帧 → 缩放至输出分辨率 → 写入 FFmpeg 管道 ----
    uint64_t encoded_count = 0;
    int row_bytes = output_width_ * 3; // BGR24 = 3 bytes/pixel
    std::vector<unsigned char> resized_buf;

    while (streaming_active_.load(std::memory_order_acquire)) {
        FrameData frame;
        bool got_frame = WaitPopFrame(frame, 100);

        if (!got_frame) {
            if (!streaming_active_.load(std::memory_order_acquire)) break;
            continue;
        }

        // ---- 毒药帧检测 ----
        if (!frame.valid) {
            spdlog::info("[{}] Poison pill received — exiting streaming loop", camera_id_);
            break;
        }

        if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
            spdlog::warn("[{}] Skipping empty frame #{}", camera_id_, frame.frame_num);
            continue;
        }

        // ---- FrameData → cv::Mat（零拷贝引用）----
        cv::Mat src_mat(static_cast<int>(frame.height),
                        static_cast<int>(frame.width),
                        CV_8UC3,
                        frame.data.data(),
                        static_cast<size_t>(frame.stride));

        // ---- 缩放到输出分辨率（若源帧与输出分辨率不同）----
        const unsigned char* write_ptr;
        if (static_cast<int>(frame.width) != output_width_ ||
            static_cast<int>(frame.height) != output_height_) {
            resized_buf.resize(row_bytes * output_height_);
            cv::Mat dst_mat(output_height_, output_width_, CV_8UC3, resized_buf.data());
            cv::resize(src_mat, dst_mat, cv::Size(output_width_, output_height_));
            write_ptr = resized_buf.data();
        } else {
            // 分辨率匹配——检查 stride 是否需要去填充
            if (static_cast<int>(frame.stride) == row_bytes) {
                // 连续内存——直接写入管道（最快路径）
                size_t written = fwrite(frame.data.data(), 1,
                                        row_bytes * output_height_, ffmpeg_pipe);
                if (written == 0) break;
                encoded_count++;
                encoded_frames_.store(encoded_count, std::memory_order_relaxed);
                if (encoded_count % 250 == 0)
                    spdlog::info("[{}] Encoded {} frames, queue: {}",
                                 camera_id_, encoded_count, QueueDepth());
                continue;
            }
            write_ptr = frame.data.data();
        }

        // ---- 逐行写入（stride != width*3 或已缩放的情况）----
        size_t written_total = 0;
        for (int y = 0; y < output_height_; y++) {
            const unsigned char* row = write_ptr + y * static_cast<int>(
                (write_ptr == resized_buf.data()) ? row_bytes : frame.stride);
            size_t w = fwrite(row, 1, row_bytes, ffmpeg_pipe);
            if (w == 0) { written_total = 0; break; }
            written_total += w;
        }
        if (written_total == 0) break;

        fflush(ffmpeg_pipe);
        encoded_count++;
        encoded_frames_.store(encoded_count, std::memory_order_relaxed);

        if (encoded_count % 250 == 0) {
            spdlog::info("[{}] Encoded {} frames, queue depth: {}",
                         camera_id_, encoded_count, QueueDepth());
        }
    }

    // ---- 关闭管道（通知 ffmpeg 输入结束，等待其完成编码和推流）----
    int close_ret = 0;
#ifdef _WIN32
    close_ret = _pclose(ffmpeg_pipe);
#else
    close_ret = pclose(ffmpeg_pipe);
#endif

    spdlog::info("[{}] FFmpeg exited (code: {}), {} frames processed",
                 camera_id_, close_ret, encoded_count);
    streaming_active_.store(false, std::memory_order_release);
}

} // namespace ingest

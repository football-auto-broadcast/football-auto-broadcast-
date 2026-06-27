/**
 * ffmpeg_frame_reader.hpp - FFmpeg 子进程 RTSP 帧读取器
 *
 * 每路 RTSP 使用独立的 FFmpeg 子进程拉流，通过命名管道读取原始 BGR24 帧。
 * 与 OpenCV VideoCapture 方案相比，FFmpeg 子进程方案：
 * - 不阻塞主线程（独立拉流线程）
 * - 单路失败不影响另一路
 * - 自动重连，连接延迟可控
 * - stop 时优雅终止 FFmpeg 子进程，不残留
 */

#ifndef VISION_EVENT_MODULE_FFMPEG_FRAME_READER_HPP
#define VISION_EVENT_MODULE_FFMPEG_FRAME_READER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

// 前向声明 OpenCV Mat
namespace cv {
class Mat;
}

namespace vision {

/**
 * @brief FFmpeg 子进程帧读取器配置
 */
struct FFmpegReaderConfig {
    std::string ffmpeg_path;        ///< ffmpeg.exe 绝对路径
    std::string stream_uri;         ///< RTSP 流地址
    std::string camera_id;          ///< 相机标识，用于日志
    int width = 1920;               ///< 帧宽度
    int height = 1080;              ///< 帧高度
    int reconnect_delay_ms = 2000;  ///< 断连后重连等待时间
};

/**
 * @brief FFmpeg 子进程帧读取器
 *
 * 每个实例管理一个 RTSP 流的拉取：
 * - 启动独立拉流线程
 * - FFmpeg 子进程输出 raw BGR24 到 stdout pipe
 * - 读取线程从 pipe 组装完整帧写入最新帧缓冲区
 * - 仅保留最新 1 帧，宁可丢帧不积压
 * - 自动重连
 */
class FFmpegFrameReader {
public:
    /**
     * @brief 构造函数
     * @param config 读取器配置
     */
    explicit FFmpegFrameReader(const FFmpegReaderConfig& config);

    ~FFmpegFrameReader();

    // 禁止拷贝
    FFmpegFrameReader(const FFmpegFrameReader&) = delete;
    FFmpegFrameReader& operator=(const FFmpegFrameReader&) = delete;

    /**
     * @brief 启动拉流线程（非阻塞，FFmpeg 连接在后台完成）
     * @return 是否成功启动
     */
    bool start();

    /**
     * @brief 停止拉流，终止 FFmpeg 子进程，等待线程退出
     */
    void stop();

    /**
     * @brief 获取最新帧（非阻塞）
     * @param image 输出 cv::Mat（BGR8, 3-channel），调用者负责保持其生命周期
     * @return true 如果自上次获取后有新的帧可用
     */
    bool get_latest_frame(cv::Mat& image, int64_t* out_timestamp_ms = nullptr);

    /**
     * @brief 自上次成功接收帧以来的毫秒数
     */
    int64_t ms_since_last_frame() const;

    /**
     * @brief 拉流线程是否在运行
     */
    bool is_running() const;

    /**
     * @brief FFmpeg 子进程当前是否在运行
     */
    bool is_ffmpeg_alive() const;

private:
    void reader_thread_func();
    bool launch_ffmpeg();
    void terminate_ffmpeg();

    FFmpegReaderConfig config_;
    size_t frame_bytes_ = 0;   ///< width * height * 3

    // 线程控制
    std::atomic<bool> running_{false};
    std::atomic<bool> ffmpeg_alive_{false};
    std::unique_ptr<std::thread> reader_thread_;

    // Windows 管道和进程句柄
#ifdef _WIN32
    HANDLE pipe_read_ = INVALID_HANDLE_VALUE;
    HANDLE pipe_write_ = INVALID_HANDLE_VALUE;
    HANDLE ffmpeg_process_ = INVALID_HANDLE_VALUE;
    HANDLE ffmpeg_thread_ = INVALID_HANDLE_VALUE;
#endif

    // 最新帧缓冲区
    mutable std::mutex frame_mutex_;
    std::vector<uint8_t> latest_frame_buffer_;
    int64_t latest_frame_ts_ms_ = 0;         // steady_clock, for internal use
    int64_t latest_frame_wall_ts_ms_ = 0;    // system_clock, for status reporting
    bool frame_available_ = false;
};

} // namespace vision

#endif // VISION_EVENT_MODULE_FFMPEG_FRAME_READER_HPP

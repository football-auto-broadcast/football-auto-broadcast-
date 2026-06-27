/**
 * ffmpeg_frame_reader.cpp — FFmpeg subprocess RTSP frame reader
 *
 * Uses FFmpeg subprocess with raw BGR24 pipe output.
 * Each frame is width*height*3 bytes. Reader thread assembles full frames
 * from the pipe and stores the latest in a mutex-protected buffer.
 *
 * Critical fixes vs. original OpenCV approach:
 * - FFmpeg subprocess isolates RTSP transport from C module process
 * - Independent thread per camera prevents mutual blocking
 * - Auto-reconnect on pipe break / FFmpeg exit
 * - frame_available_ flag with mutex ensures safe handoff
 * - system_clock timestamps for compatibility with get_status()
 */

#include "ffmpeg_frame_reader.hpp"

#include <opencv2/core.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

namespace vision {

namespace {

int64_t steady_now_ms() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

int64_t wall_now_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

} // namespace

FFmpegFrameReader::FFmpegFrameReader(const FFmpegReaderConfig& config)
    : config_(config)
    , frame_bytes_(static_cast<size_t>(config.width) *
                   static_cast<size_t>(config.height) * 3) {
    latest_frame_buffer_.resize(frame_bytes_);
    fprintf(stderr, "[ffmpeg-reader][%s] init: %dx%d frame_bytes=%zu\n",
            config_.camera_id.c_str(), config_.width, config_.height, frame_bytes_);
    fflush(stderr);
}

FFmpegFrameReader::~FFmpegFrameReader() { stop(); }

bool FFmpegFrameReader::start() {
    if (running_.load()) return true;
    if (config_.stream_uri.empty() || frame_bytes_ == 0) return false;
    running_.store(true);
    reader_thread_ = std::make_unique<std::thread>(
        &FFmpegFrameReader::reader_thread_func, this);
    return true;
}

void FFmpegFrameReader::stop() {
    running_.store(false);
    terminate_ffmpeg();
    if (reader_thread_ && reader_thread_->joinable()) {
        reader_thread_->join();
    }
    reader_thread_.reset();
}

bool FFmpegFrameReader::get_latest_frame(cv::Mat& image, int64_t* out_timestamp_ms) {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (!frame_available_) {
        return false;
    }
    image = cv::Mat(config_.height, config_.width, CV_8UC3);
    if (image.empty()) {
        std::cerr << "[ffmpeg-reader][" << config_.camera_id
                  << "] cv::Mat allocation failed" << std::endl;
        return false;
    }
    std::memcpy(image.data, latest_frame_buffer_.data(), frame_bytes_);
    if (out_timestamp_ms) {
        *out_timestamp_ms = latest_frame_wall_ts_ms_;
    }
    frame_available_ = false;
    return true;
}

int64_t FFmpegFrameReader::ms_since_last_frame() const {
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_frame_ts_ms_ == 0) return INT64_MAX;
    int64_t elapsed = steady_now_ms() - latest_frame_ts_ms_;
    return elapsed > 0 ? elapsed : 0;
}

bool FFmpegFrameReader::is_running() const { return running_.load(); }
bool FFmpegFrameReader::is_ffmpeg_alive() const { return ffmpeg_alive_.load(); }

// ============================================================================
// Reader thread
// ============================================================================

void FFmpegFrameReader::reader_thread_func() {
    std::vector<uint8_t> assembly_buffer(frame_bytes_);
    size_t assembly_offset = 0;
    int64_t total_assembled = 0;

    while (running_.load()) {
        if (!launch_ffmpeg()) {
            int64_t deadline = steady_now_ms() + config_.reconnect_delay_ms;
            while (running_.load() && steady_now_ms() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        ffmpeg_alive_.store(true);
        assembly_offset = 0;
        std::cout << "[ffmpeg-reader][" << config_.camera_id
                  << "] ffmpeg launched, reading frames..." << std::endl;

        while (running_.load() && ffmpeg_alive_.load()) {
#ifdef _WIN32
            // Non-blocking read: poll pipe with PeekNamedPipe before blocking ReadFile.
            // This prevents hanging if FFmpeg stalls without closing the pipe.
            DWORD bytes_avail = 0;
            int64_t poll_start = steady_now_ms();
            bool data_ready = false;

            while (running_.load() && ffmpeg_alive_.load()) {
                if (PeekNamedPipe(pipe_read_, nullptr, 0, nullptr, &bytes_avail, nullptr)) {
                    if (bytes_avail > 0) { data_ready = true; break; }
                } else {
                    break; // pipe error
                }
                // Check FFmpeg liveness during poll wait
                DWORD exit_code = 0;
                if (ffmpeg_process_ != INVALID_HANDLE_VALUE &&
                    GetExitCodeProcess(ffmpeg_process_, &exit_code) &&
                    exit_code != STILL_ACTIVE) {
                    std::cerr << "[ffmpeg-reader][" << config_.camera_id
                              << "] ffmpeg exited code=" << exit_code << " during poll" << std::endl;
                    break;
                }
                // Timeout after 3s of no data
                if (steady_now_ms() - poll_start > 3000) {
                    std::cerr << "[ffmpeg-reader][" << config_.camera_id
                              << "] pipe stalled (no data for 3s)" << std::endl;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!data_ready) break; // pipe error or timeout

            // Now do a quick blocking read of available data
            DWORD to_read = (std::min)(bytes_avail,
                static_cast<DWORD>(frame_bytes_ - assembly_offset));
            DWORD bytes_read = 0;
            BOOL read_ok = ReadFile(pipe_read_,
                                    assembly_buffer.data() + assembly_offset,
                                    to_read, &bytes_read, nullptr);

            if (!read_ok || bytes_read == 0) {
                if (!read_ok) {
                    DWORD err = GetLastError();
                    if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                        std::cerr << "[ffmpeg-reader][" << config_.camera_id
                                  << "] pipe broken" << std::endl;
                    } else if (err != ERROR_OPERATION_ABORTED) {
                        std::cerr << "[ffmpeg-reader][" << config_.camera_id
                                  << "] ReadFile error " << err << std::endl;
                    }
                }
                break;
            }

            assembly_offset += bytes_read;
#endif
            // Check if FFmpeg exited (post-read check)
            if (ffmpeg_process_ != INVALID_HANDLE_VALUE) {
                DWORD exit_code = 0;
                if (GetExitCodeProcess(ffmpeg_process_, &exit_code) &&
                    exit_code != STILL_ACTIVE) {
                    std::cerr << "[ffmpeg-reader][" << config_.camera_id
                              << "] ffmpeg exited code=" << exit_code << std::endl;
                    break;
                }
            }

            // Full frame assembled
            if (assembly_offset >= frame_bytes_) {
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    std::memcpy(latest_frame_buffer_.data(),
                                assembly_buffer.data(),
                                frame_bytes_);
                    latest_frame_ts_ms_ = steady_now_ms();
                    latest_frame_wall_ts_ms_ = wall_now_ms();
                    frame_available_ = true;
                }

                ++total_assembled;
                if (total_assembled <= 5 || total_assembled % 25 == 0) {
                    fprintf(stderr, "[ffmpeg-reader][%s] frame #%lld ready\n",
                            config_.camera_id.c_str(), (long long)total_assembled);
                    fflush(stderr);
                }

                // Shift extra bytes to beginning of buffer
                if (assembly_offset > frame_bytes_) {
                    size_t extra = assembly_offset - frame_bytes_;
                    if (extra > 0 && extra < frame_bytes_) {
                        std::memmove(assembly_buffer.data(),
                                     assembly_buffer.data() + frame_bytes_,
                                     extra);
                    }
                    assembly_offset = extra;
                } else {
                    assembly_offset = 0;
                }
            }
        }

        terminate_ffmpeg();
        ffmpeg_alive_.store(false);
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            frame_available_ = false;
        }

        if (running_.load()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.reconnect_delay_ms));
        }
    }
}

// ============================================================================
// FFmpeg subprocess
// ============================================================================

#ifdef _WIN32

bool FFmpegFrameReader::launch_ffmpeg() {
    if (!running_.load()) return false;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE child_stdout_read = INVALID_HANDLE_VALUE;
    HANDLE child_stdout_write = INVALID_HANDLE_VALUE;

    // The default anonymous-pipe buffer is only a few KB. A 1080p BGR24 frame
    // is about 6 MB, so the default causes thousands of producer/consumer
    // context switches per frame and makes the reader appear stale.
    constexpr DWORD kPipeBufferBytes = 1024 * 1024;
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, kPipeBufferBytes)) {
        std::cerr << "[ffmpeg-reader][" << config_.camera_id
                  << "] CreatePipe failed: " << GetLastError() << std::endl;
        return false;
    }

    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    // Build FFmpeg command
    // -loglevel error: suppress ALL stderr output to avoid pipe clogging
    // pipe:1 = stdout = raw BGR24 frames
    std::ostringstream cmd;
    cmd << "\"" << config_.ffmpeg_path << "\""
        << " -loglevel error"
        << " -rtsp_transport tcp"
        << " -fflags nobuffer"
        << " -flags low_delay"
        << " -i " << config_.stream_uri
        << " -an -sn -dn"
        << " -pix_fmt bgr24"
        << " -f rawvideo"
        << " pipe:1";

    std::string cmd_str = cmd.str();
    std::vector<char> cmd_buf(cmd_str.size() + 1);
    std::memcpy(cmd_buf.data(), cmd_str.c_str(), cmd_str.size() + 1);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = child_stdout_write;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL created = CreateProcessA(
        nullptr, cmd_buf.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(child_stdout_write);

    if (!created) {
        std::cerr << "[ffmpeg-reader][" << config_.camera_id
                  << "] CreateProcess failed: " << GetLastError() << std::endl;
        CloseHandle(child_stdout_read);
        return false;
    }

    CloseHandle(pi.hThread);
    pipe_read_ = child_stdout_read;
    ffmpeg_process_ = pi.hProcess;
    return true;
}

void FFmpegFrameReader::terminate_ffmpeg() {
    if (pipe_read_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_read_);
        pipe_read_ = INVALID_HANDLE_VALUE;
    }
    if (ffmpeg_process_ != INVALID_HANDLE_VALUE) {
        TerminateProcess(ffmpeg_process_, 0);
        WaitForSingleObject(ffmpeg_process_, 3000);
        CloseHandle(ffmpeg_process_);
        ffmpeg_process_ = INVALID_HANDLE_VALUE;
    }
}

#else
bool FFmpegFrameReader::launch_ffmpeg() { return false; }
void FFmpegFrameReader::terminate_ffmpeg() {}
#endif

} // namespace vision

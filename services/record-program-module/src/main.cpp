#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using steady_clock = std::chrono::steady_clock;

namespace {

struct AppConfig {
    int server_port = 8082;
    std::string data_root = "D:\\football\\data";
    std::string preview_url = "rtsp://127.0.0.1:8560/program";
    int output_width = 1920;
    int output_height = 1080;
    int fps = 25;
    double ema_alpha = 0.15;
    int decision_timeout_ms = 3000;
};

struct MatchConfig {
    std::string match_id;
    std::string cam_01_uri = "rtsp://127.0.0.1:8554/main";
    std::string cam_02_uri = "rtsp://127.0.0.1:8555/aux";
    std::string raw_root;
    std::string program_root;
    std::string metadata_root;
};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

json ok(json data = json::object()) {
    return json{{"code", 0}, {"message", "ok"}, {"data", data}};
}

json err(int code, const std::string& message) {
    return json{{"code", code}, {"message", message}, {"data", json::object()}};
}

std::string join_path(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

std::string find_ffmpeg_path() {
    fs::path probe = fs::current_path();
    for (int i = 0; i < 8; ++i) {
        fs::path candidate = probe / "third_party" / "windows" / "ffmpeg" / "bin" / "ffmpeg.exe";
        if (fs::exists(candidate)) return candidate.string();
        if (!probe.has_parent_path() || probe.parent_path() == probe) break;
        probe = probe.parent_path();
    }
    return "ffmpeg";
}

std::string quote_arg(const std::string& value) {
    return "\"" + value + "\"";
}

double disk_free_gb_for(const std::string& root) {
    try {
        fs::path p(root.empty() ? "." : root);
        if (!fs::exists(p)) {
            fs::create_directories(p);
        }
        const auto space = fs::space(p);
        return static_cast<double>(space.available) / (1024.0 * 1024.0 * 1024.0);
    } catch (...) {
        return 0.0;
    }
}

cv::Rect make_crop_rect(int width, int height, double cx, double cy) {
    width = std::max(1, width);
    height = std::max(1, height);
    int crop_w = width;
    int crop_h = static_cast<int>(crop_w * 9.0 / 16.0);
    if (crop_h > height) {
        crop_h = height;
        crop_w = static_cast<int>(crop_h * 16.0 / 9.0);
    }
    crop_w = std::max(1, std::min(crop_w, width));
    crop_h = std::max(1, std::min(crop_h, height));

    int center_x = static_cast<int>(cx * width);
    int center_y = static_cast<int>(cy * height);
    int x = std::clamp(center_x - crop_w / 2, 0, std::max(0, width - crop_w));
    int y = std::clamp(center_y - crop_h / 2, 0, std::max(0, height - crop_h));
    return cv::Rect(x, y, crop_w, crop_h);
}

class FfmpegMp4Writer {
public:
    bool open(const std::string& output_path, int fps, const cv::Size& size) {
        if (output_path.empty() || fps <= 0 || size.width <= 0 || size.height <= 0) {
            return false;
        }
        fs::create_directories(fs::path(output_path).parent_path());
        const std::string command =
            quote_arg(find_ffmpeg_path()) +
            " -y -loglevel error -f rawvideo -pixel_format bgr24 -video_size " +
            std::to_string(size.width) + "x" + std::to_string(size.height) +
            " -framerate " + std::to_string(fps) +
            " -i - -an -c:v libx264 -preset ultrafast -pix_fmt yuv420p -movflags +faststart " +
            quote_arg(output_path);
        return open_process(command, size);
    }

    bool open_preview(const std::string& output_url, int fps, const cv::Size& size) {
        if (output_url.empty() || fps <= 0 || size.width <= 0 || size.height <= 0) {
            return false;
        }
        const std::string command =
            quote_arg(find_ffmpeg_path()) +
            " -y -loglevel error -f rawvideo -pixel_format bgr24 -video_size " +
            std::to_string(size.width) + "x" + std::to_string(size.height) +
            " -framerate " + std::to_string(fps) +
            " -i - -an -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p "
            "-rtsp_transport tcp -f rtsp " + quote_arg(output_url);
        return open_process(command, size);
    }

    bool isOpened() const {
#ifdef _WIN32
        return process && stdin_write && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
#else
        return false;
#endif
    }

    bool write(const cv::Mat& frame) {
        if (!isOpened() || frame.empty()) return false;
        cv::Mat writable = frame;
        cv::Mat resized;
        cv::Mat contiguous;
        if (frame.size() != frame_size) {
            cv::resize(frame, resized, frame_size);
            writable = resized;
        }
        if (!writable.isContinuous()) {
            contiguous = writable.clone();
            writable = contiguous;
        }
        const size_t bytes = writable.total() * writable.elemSize();
#ifdef _WIN32
        size_t offset = 0;
        while (offset < bytes) {
            const DWORD chunk = static_cast<DWORD>((std::min)(
                bytes - offset, static_cast<size_t>(1024 * 1024)));
            DWORD written = 0;
            if (!WriteFile(stdin_write, writable.data + offset, chunk, &written, nullptr) ||
                written == 0) {
                return false;
            }
            offset += written;
        }
        return true;
#else
        return false;
#endif
    }

    void close() {
#ifdef _WIN32
        if (stdin_write) {
            CloseHandle(stdin_write);
            stdin_write = nullptr;
        }
        if (process) {
            if (WaitForSingleObject(process, 5000) == WAIT_TIMEOUT) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 2000);
            }
            CloseHandle(process);
            process = nullptr;
        }
        if (thread) {
            CloseHandle(thread);
            thread = nullptr;
        }
#endif
    }

    ~FfmpegMp4Writer() {
        close();
    }

private:
    bool open_process(const std::string& command, const cv::Size& size) {
        close();
#ifndef _WIN32
        (void)command;
        (void)size;
        return false;
#else
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE stdin_read = nullptr;
        constexpr DWORD kPipeBufferBytes = 1024 * 1024;
        if (!CreatePipe(&stdin_read, &stdin_write, &sa, kPipeBufferBytes)) {
            return false;
        }
        SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

        HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = stdin_read;
        si.hStdOutput = nul != INVALID_HANDLE_VALUE ? nul : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = nul != INVALID_HANDLE_VALUE ? nul : GetStdHandle(STD_ERROR_HANDLE);

        std::vector<char> mutable_command(command.begin(), command.end());
        mutable_command.push_back('\0');
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr,
                                            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(stdin_read);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!created) {
            CloseHandle(stdin_write);
            stdin_write = nullptr;
            return false;
        }

        process = pi.hProcess;
        thread = pi.hThread;
        frame_size = size;
        return true;
#endif
    }

#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE stdin_write = nullptr;
#endif
    cv::Size frame_size;
};

bool open_writer(FfmpegMp4Writer& writer,
                 const std::string& path,
                 int fps,
                 const cv::Size& size) {
    return writer.open(path, fps, size);
}

class FfmpegRtspRecorder {
public:
    bool start(const std::string& input_uri, const std::string& output_path) {
        stop();
#ifndef _WIN32
        return false;
#else
        fs::create_directories(fs::path(output_path).parent_path());

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE stdin_read = nullptr;
        if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
            return false;
        }
        SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

        HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = stdin_read;
        si.hStdOutput = (nul != INVALID_HANDLE_VALUE) ? nul : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = (nul != INVALID_HANDLE_VALUE) ? nul : GetStdHandle(STD_ERROR_HANDLE);

        std::string command =
            quote_arg(find_ffmpeg_path()) +
            " -y -loglevel warning -rtsp_transport tcp -i " + input_uri +
            " -an -c:v copy -movflags +faststart " + quote_arg(output_path);
        std::vector<char> mutable_cmd(command.begin(), command.end());
        mutable_cmd.push_back('\0');

        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(stdin_read);
        if (nul != INVALID_HANDLE_VALUE) {
            CloseHandle(nul);
        }
        if (!ok) {
            CloseHandle(stdin_write);
            stdin_write = nullptr;
            return false;
        }

        process = pi.hProcess;
        thread = pi.hThread;
        path = output_path;
        return true;
#endif
    }

    bool isRunning() const {
#ifndef _WIN32
        return false;
#else
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
#endif
    }

    void stop() {
#ifdef _WIN32
        if (stdin_write) {
            DWORD written = 0;
            const char quit[] = "q\n";
            WriteFile(stdin_write, quit, static_cast<DWORD>(sizeof(quit) - 1), &written, nullptr);
            CloseHandle(stdin_write);
            stdin_write = nullptr;
        }
        if (process) {
            DWORD wait = WaitForSingleObject(process, 5000);
            if (wait == WAIT_TIMEOUT) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 2000);
            }
            CloseHandle(process);
            process = nullptr;
        }
        if (thread) {
            CloseHandle(thread);
            thread = nullptr;
        }
#endif
    }

    ~FfmpegRtspRecorder() {
        stop();
    }

private:
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE stdin_write = nullptr;
#endif
    std::string path;
};

class CameraUnit {
public:
    explicit CameraUnit(std::string camera_id) : id(std::move(camera_id)) {}

    bool start(const std::string& uri,
               const std::string& raw_path,
               const std::string& cut_path,
               const AppConfig& cfg) {
        source_uri = uri;
        this->raw_path = raw_path;
        this->cut_path = cut_path;
        output_size = cv::Size(cfg.output_width, cfg.output_height);
        target_fps = cfg.fps;
        ema_alpha = cfg.ema_alpha;

        if (!raw_recorder.start(uri, raw_path)) {
            status = "failed";
            last_warning = "failed to start raw recorder: " + raw_path;
            return false;
        }

        running = true;
        status = "degraded";
        last_warning = "preview input pending: " + uri;
        last_open_attempt = steady_clock::now() - std::chrono::milliseconds(2000);
        worker = std::thread(&CameraUnit::loop, this);
        return true;
    }

    void stop() {
        running = false;
        if (worker.joinable()) worker.join();
        if (cap.isOpened()) cap.release();
        raw_recorder.stop();
        try {
            if (fs::exists(raw_path)) {
                fs::create_directories(fs::path(cut_path).parent_path());
                fs::copy_file(raw_path, cut_path, fs::copy_options::overwrite_existing);
            }
        } catch (const std::exception& e) {
            last_warning = std::string("failed to copy raw to cut: ") + e.what();
        }
        if (status != "failed") status = "stopped";
    }

    void set_focus_region(int x, int y, int width, int height) {
        std::lock_guard<std::mutex> lock(mtx);
        if (frame_width <= 0 || frame_height <= 0) {
            target_x = 0.5;
            target_y = 0.5;
        } else {
            const double cx = (static_cast<double>(x) + width / 2.0) / frame_width;
            const double cy = (static_cast<double>(y) + height / 2.0) / frame_height;
            target_x = std::clamp(cx, 0.0, 1.0);
            target_y = std::clamp(cy, 0.0, 1.0);
        }
        last_focus_update = steady_clock::now();
    }

    cv::Mat latest_cut() {
        std::lock_guard<std::mutex> lock(mtx);
        return shared_cut.empty() ? cv::Mat() : shared_cut.clone();
    }

    json status_json() const {
        std::lock_guard<std::mutex> lock(mtx);
        return json{
            {"camera_id", id},
            {"status", status},
            {"fps", measured_fps},
            {"frame_width", frame_width},
            {"frame_height", frame_height},
            {"raw_path", raw_path},
            {"cut_path", cut_path},
            {"last_warning", last_warning}
        };
    }

    std::string id;
    std::string status = "idle";
    std::string last_warning;
    std::string raw_path;
    std::string cut_path;

private:
    bool open_capture() {
        if (cap.isOpened()) {
            cap.release();
        }
        const std::vector<int> open_params = {
            cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
            cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000,
            cv::CAP_PROP_HW_ACCELERATION, static_cast<int>(cv::VIDEO_ACCELERATION_ANY)
        };
        last_open_attempt = steady_clock::now();
        return cap.open(source_uri, cv::CAP_FFMPEG, open_params);
    }

    void loop() {
        cv::Mat frame;
        int64_t last_fps_ms = now_ms();
        int frames_since_fps = 0;
        int consecutive_read_failures = 0;

        while (running) {
            if (!cap.read(frame) || frame.empty()) {
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    status = "degraded";
                    last_warning = "empty frame from " + source_uri;
                    measured_fps = 0.0;
                }
                ++consecutive_read_failures;
                if (consecutive_read_failures >= 3 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        steady_clock::now() - last_open_attempt).count() >= 1500) {
                    if (!open_capture()) {
                        std::lock_guard<std::mutex> lock(mtx);
                        last_warning = "reopen failed for " + source_uri;
                    } else {
                        std::lock_guard<std::mutex> lock(mtx);
                        last_warning = "reopened input after empty frames: " + source_uri;
                    }
                    consecutive_read_failures = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            consecutive_read_failures = 0;

            {
                std::lock_guard<std::mutex> lock(mtx);
                frame_width = frame.cols;
                frame_height = frame.rows;
            }

            double local_tx;
            double local_ty;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        steady_clock::now() - last_focus_update).count() > 1000) {
                    target_x = 0.5;
                    target_y = 0.5;
                }
                current_x += (target_x - current_x) * ema_alpha;
                current_y += (target_y - current_y) * ema_alpha;
                local_tx = current_x;
                local_ty = current_y;
            }

            cv::Rect crop_rect = make_crop_rect(frame.cols, frame.rows, local_tx, local_ty);
            cv::Mat cut = frame(crop_rect).clone();
            cv::resize(cut, cut, output_size);
            {
                std::lock_guard<std::mutex> lock(mtx);
                shared_cut = cut;
                status = "ready";
            }

            ++frames_since_fps;
            const int64_t current_ms = now_ms();
            if (current_ms - last_fps_ms >= 1000) {
                std::lock_guard<std::mutex> lock(mtx);
                measured_fps = frames_since_fps * 1000.0 / std::max<int64_t>(1, current_ms - last_fps_ms);
                frames_since_fps = 0;
                last_fps_ms = current_ms;
            }
        }
    }

    cv::VideoCapture cap;
    FfmpegRtspRecorder raw_recorder;
    std::thread worker;
    std::atomic<bool> running{false};
    std::string source_uri;
    cv::Size output_size{1920, 1080};
    int target_fps = 25;
    double ema_alpha = 0.15;

    mutable std::mutex mtx;
    cv::Mat shared_cut;
    int frame_width = 0;
    int frame_height = 0;
    double target_x = 0.5;
    double target_y = 0.5;
    double current_x = 0.5;
    double current_y = 0.5;
    double measured_fps = 0.0;
    steady_clock::time_point last_focus_update = steady_clock::now();
    steady_clock::time_point last_open_attempt = steady_clock::now();
};

class RecordManager {
public:
    explicit RecordManager(AppConfig cfg) : cfg(std::move(cfg)), cam_01("cam_01"), cam_02("cam_02") {}

    bool init(const json& request, std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        if (recording) {
            message = "recording already running";
            return false;
        }

        match_cfg = MatchConfig{};
        match_cfg.match_id = request.value("match_id", "");
        if (match_cfg.match_id.empty()) {
            message = "match_id required";
            return false;
        }

        match_cfg.raw_root = cfg.data_root + "\\raw";
        match_cfg.program_root = cfg.data_root + "\\program";
        match_cfg.metadata_root = cfg.data_root + "\\metadata";

        if (request.contains("storage_config")) {
            const auto& storage = request["storage_config"];
            match_cfg.raw_root = storage.value("raw_root", match_cfg.raw_root);
            match_cfg.program_root = storage.value("program_root", match_cfg.program_root);
            match_cfg.metadata_root = storage.value("metadata_root", match_cfg.metadata_root);
        }
        if (request.contains("program_config")) {
            const auto& program = request["program_config"];
            cfg.fps = program.value("fps", cfg.fps);
            std::string resolution = program.value("output_resolution", std::string("1920x1080"));
            if (resolution == "1920x1080") {
                cfg.output_width = 1920;
                cfg.output_height = 1080;
            }
        }
        if (request.contains("input_streams")) {
            for (const auto& stream : request["input_streams"]) {
                const std::string camera_id = stream.value("camera_id", "");
                const std::string uri = stream.value("stream_uri", "");
                if (camera_id == "cam_01" && !uri.empty()) match_cfg.cam_01_uri = uri;
                if (camera_id == "cam_02" && !uri.empty()) match_cfg.cam_02_uri = uri;
            }
        }

        fs::create_directories(raw_dir());
        fs::create_directories(program_dir());
        fs::create_directories(metadata_dir());
        status = "initializing";
        last_warning.clear();
        message = "ok";
        return true;
    }

    bool start(std::string match_id, std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        if (match_cfg.match_id.empty()) {
            if (match_id.empty()) {
                message = "match not initialized";
                return false;
            }
            match_cfg.match_id = std::move(match_id);
            match_cfg.raw_root = cfg.data_root + "\\raw";
            match_cfg.program_root = cfg.data_root + "\\program";
            match_cfg.metadata_root = cfg.data_root + "\\metadata";
        }
        if (match_id != match_cfg.match_id) {
            message = "match_id mismatch";
            return false;
        }
        if (recording) {
            message = "already recording";
            return true;
        }

        record_start_ms = now_ms();
        current_program_camera_id = "cam_01";
        last_decision_update = steady_clock::now();

        const bool cam1_ok = cam_01.start(match_cfg.cam_01_uri, cam_01_raw_path(), cam_01_cut_path(), cfg);
        const bool cam2_ok = cam_02.start(match_cfg.cam_02_uri, cam_02_raw_path(), cam_02_cut_path(), cfg);
        if (!cam1_ok && !cam2_ok) {
            status = "failed";
            message = "both inputs unavailable";
            return false;
        }

        if (!open_writer(program_writer, program_path(), cfg.fps, cv::Size(cfg.output_width, cfg.output_height))) {
            status = "failed";
            message = "failed to open program writer";
            return false;
        }

        if (!preview_writer.open_preview(cfg.preview_url, cfg.fps,
                                         cv::Size(cfg.output_width, cfg.output_height))) {
            last_warning = "failed to start preview ffmpeg";
        }
        recording = true;
        status = (cam1_ok && cam2_ok) ? "recording" : "degraded";
        program_worker = std::thread(&RecordManager::program_loop, this);
        message = "ok";
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (!recording && status != "recording" && status != "degraded") return;
            recording = false;
        }
        if (program_worker.joinable()) {
            program_worker.join();
        }
        preview_writer.close();
        program_writer.close();
        cam_01.stop();
        cam_02.stop();

        std::lock_guard<std::mutex> lock(mtx);
        record_end_ms = now_ms();
        write_record_index_locked();
        status = "stopped";
    }

    void update_focus_regions(const json& request) {
        if (!request.contains("regions")) return;
        for (const auto& region : request["regions"]) {
            const std::string camera_id = region.value("camera_id", "");
            if (!region.contains("focus_region")) continue;
            const auto& rect = region["focus_region"];
            const int x = std::max(0, rect.value("x", 0));
            const int y = std::max(0, rect.value("y", 0));
            const int width = std::max(1, rect.value("width", 1));
            const int height = std::max(1, rect.value("height", 1));
            if (camera_id == "cam_01") cam_01.set_focus_region(x, y, width, height);
            if (camera_id == "cam_02") cam_02.set_focus_region(x, y, width, height);
        }
    }

    void update_program_decision(const json& request) {
        const std::string recommended = request.value("recommended_camera_id", "cam_01");
        std::lock_guard<std::mutex> lock(mtx);
        if (recommended == "cam_01" || recommended == "cam_02") {
            current_program_camera_id = recommended;
            last_decision_update = steady_clock::now();
        }
    }

    json status_response() const {
        std::lock_guard<std::mutex> lock(mtx);
        const double duration = record_start_ms > 0
            ? (static_cast<double>((recording ? now_ms() : record_end_ms) - record_start_ms) / 1000.0)
            : 0.0;
        return json{
            {"match_id", match_cfg.match_id},
            {"status", status},
            {"cam_01_cut_status", cam_01.status},
            {"cam_02_cut_status", cam_02.status},
            {"current_program_camera_id", current_program_camera_id},
            {"program_output_status", recording ? "online" : "stopped"},
            {"record_duration_sec", duration},
            {"disk_free_gb", disk_free_gb_for(cfg.data_root)},
            {"last_warning", last_warning},
            {"cam_01", cam_01.status_json()},
            {"cam_02", cam_02.status_json()}
        };
    }

    json files_response() const {
        std::lock_guard<std::mutex> lock(mtx);
        return json{
            {"match_id", match_cfg.match_id},
            {"record_index_path", record_index_path()},
            {"raw_files", json::array({
                json{{"camera_id", "cam_01"}, {"file_path", cam_01_raw_path()}},
                json{{"camera_id", "cam_02"}, {"file_path", cam_02_raw_path()}}
            })},
            {"cut_files", json::array({
                json{{"camera_id", "cam_01"}, {"file_path", cam_01_cut_path()}},
                json{{"camera_id", "cam_02"}, {"file_path", cam_02_cut_path()}}
            })},
            {"program_files", json::array({json{{"file_path", program_path()}}})}
        };
    }

private:
    void program_loop() {
        const auto frame_interval = std::chrono::milliseconds(std::max(1, 1000 / cfg.fps));
        auto next_frame_time = steady_clock::now();
        while (recording) {
            next_frame_time += frame_interval;
            std::string selected;
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        steady_clock::now() - last_decision_update).count() > cfg.decision_timeout_ms) {
                    current_program_camera_id = "cam_01";
                }
                selected = current_program_camera_id;
            }

            cv::Mat frame = selected == "cam_02" ? cam_02.latest_cut() : cam_01.latest_cut();
            if (frame.empty()) {
                frame = selected == "cam_02" ? cam_01.latest_cut() : cam_02.latest_cut();
            }
            if (!frame.empty()) {
                if (program_writer.isOpened()) program_writer.write(frame);
                if (preview_writer.isOpened()) preview_writer.write(frame);
            }
            std::this_thread::sleep_until(next_frame_time);
        }
    }

    void write_record_index_locked() const {
        const double duration = record_start_ms > 0 && record_end_ms >= record_start_ms
            ? (record_end_ms - record_start_ms) / 1000.0
            : 0.0;
        json index = {
            {"match_id", match_cfg.match_id},
            {"record_start_timestamp_ms", record_start_ms},
            {"record_end_timestamp_ms", record_end_ms},
            {"duration_sec", duration},
            {"video_codec", "h264"},
            {"container", "mp4"},
            {"fps", cfg.fps},
            {"source_resolution", "source_native"},
            {"program_resolution", std::to_string(cfg.output_width) + "x" + std::to_string(cfg.output_height)},
            {"cam_01_raw_path", cam_01_raw_path()},
            {"cam_02_raw_path", cam_02_raw_path()},
            {"cam_01_cut_path", cam_01_cut_path()},
            {"cam_02_cut_path", cam_02_cut_path()},
            {"program_path", program_path()},
            {"status", "success"}
        };
        fs::create_directories(metadata_dir());
        std::ofstream out(record_index_path(), std::ios::trunc);
        out << index.dump(2);
    }

    fs::path raw_dir() const { return fs::path(match_cfg.raw_root) / match_cfg.match_id; }
    fs::path program_dir() const { return fs::path(match_cfg.program_root) / match_cfg.match_id; }
    fs::path metadata_dir() const { return fs::path(match_cfg.metadata_root) / match_cfg.match_id; }
    std::string cam_01_raw_path() const { return (raw_dir() / "cam_01.mp4").string(); }
    std::string cam_02_raw_path() const { return (raw_dir() / "cam_02.mp4").string(); }
    std::string cam_01_cut_path() const { return (program_dir() / "cam_01_cut.mp4").string(); }
    std::string cam_02_cut_path() const { return (program_dir() / "cam_02_cut.mp4").string(); }
    std::string program_path() const { return (program_dir() / "program.mp4").string(); }
    std::string record_index_path() const { return (metadata_dir() / "record_index.json").string(); }

    AppConfig cfg;
    MatchConfig match_cfg;
    CameraUnit cam_01;
    CameraUnit cam_02;
    mutable std::mutex mtx;
    std::atomic<bool> recording{false};
    std::thread program_worker;
    FfmpegMp4Writer program_writer;
    FfmpegMp4Writer preview_writer;
    std::string status = "idle";
    std::string last_warning;
    std::string current_program_camera_id = "cam_01";
    int64_t record_start_ms = 0;
    int64_t record_end_ms = 0;
    steady_clock::time_point last_decision_update = steady_clock::now();
};

AppConfig load_config() {
    AppConfig cfg;
    std::ifstream in("config.json");
    if (!in.is_open()) return cfg;
    try {
        json j = json::parse(in);
        cfg.server_port = j.value("server_port", cfg.server_port);
        cfg.data_root = j.value("data_root", cfg.data_root);
        cfg.preview_url = j.value("rtsp_preview_url", cfg.preview_url);
        cfg.ema_alpha = j.value("ema_alpha", cfg.ema_alpha);
        cfg.fps = j.value("target_fps", cfg.fps);
    } catch (...) {
    }
    return cfg;
}

} // namespace

int main() {
    AppConfig cfg = load_config();
    RecordManager manager(cfg);
    httplib::Server server;

    server.Post("/api/v1/record/matches/init", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string message;
            if (!manager.init(json::parse(req.body), message)) {
                res.set_content(err(1001, message).dump(), "application/json");
                return;
            }
            res.set_content(ok({{"initialized", true}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.set_content(err(1001, e.what()).dump(), "application/json");
        }
    });

    server.Post(R"(/api/v1/record/matches/([^/]+)/start)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string message;
        const std::string match_id = req.matches[1];
        if (!manager.start(match_id, message)) {
            res.set_content(err(1003, message).dump(), "application/json");
            return;
        }
        res.set_content(ok({{"started", true}}).dump(), "application/json");
    });

    server.Post(R"(/api/v1/record/matches/([^/]+)/stop)", [&](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        manager.stop();
        res.set_content(ok({{"stopped", true}}).dump(), "application/json");
    });

    server.Post(R"(/api/v1/record/matches/([^/]+)/focus-regions)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            manager.update_focus_regions(json::parse(req.body));
            res.set_content(ok({{"received", true}, {"applied", true}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.set_content(err(1001, e.what()).dump(), "application/json");
        }
    });

    server.Post(R"(/api/v1/record/matches/([^/]+)/program-decision)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            manager.update_program_decision(json::parse(req.body));
            res.set_content(ok({{"received", true}, {"applied", true}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.set_content(err(1001, e.what()).dump(), "application/json");
        }
    });

    server.Get(R"(/api/v1/record/matches/([^/]+)/status)", [&](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        res.set_content(ok(manager.status_response()).dump(), "application/json");
    });

    server.Get(R"(/api/v1/record/matches/([^/]+)/files)", [&](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        res.set_content(ok(manager.files_response()).dump(), "application/json");
    });

    server.Get("/api/v1/record/status", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(ok(manager.status_response()).dump(), "application/json");
    });

    server.listen("0.0.0.0", cfg.server_port);
    return 0;
}

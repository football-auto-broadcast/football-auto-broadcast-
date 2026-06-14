#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

// 使用标准的英文半角双引号，指向本地单头文件
#include "httplib.h"
#include "json.hpp"
#include <opencv2/opencv.hpp>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace std::chrono;

// 格式化路径为 Windows 反斜杠
std::string formatWindowsPath(std::string p) {
    std::replace(p.begin(), p.end(), '/', '\\');
    return p;
}

// 严格遵守 v1.2 Frozen 配置与命名字段
struct AppConfig {
    int server_port = 8082;
    std::string data_root = "V:/Football_Storage";
    std::string cam_01_src = "rtsp://127.0.0.1:8554/main";
    std::string cam_02_src = "rtsp://127.0.0.1:8555/aux";
    std::string rtsp_preview_url = "rtsp://127.0.0.1:8560/program";
    float ema_alpha = 0.12f;
    int output_width = 1920;
    int output_height = 1080;
    int target_fps = 25;
} cfg;

bool loadConfig() {
    std::string path = "config.json";
    try {
        if (!fs::exists(path)) return false;
        std::ifstream ifs(path);
        json j = json::parse(ifs);
        cfg.server_port = j.value("server_port", 8082);
        cfg.data_root = j.value("data_root", "V:/Football_Storage");
        cfg.cam_01_src = j.value("cam_01_src", "rtsp://127.0.0.1:8554/main");
        cfg.cam_02_src = j.value("cam_02_src", "rtsp://127.0.0.1:8555/aux");
        cfg.rtsp_preview_url = j.value("rtsp_preview_url", "rtsp://127.0.0.1:8560/program");
        cfg.ema_alpha = j.value("ema_alpha", 0.12f);
        return true;
    } catch (...) { return false; }
}

class CameraUnit {
public:
    std::string id;
    cv::VideoCapture cap;
    cv::VideoWriter raw_w, cut_w;
    std::atomic<bool> is_running{false};
    std::thread capture_thread;

    cv::Mat shared_frame;
    std::mutex mtx;

    float tx = 0.5f, ty = 0.5f;
    float cx = 0.5f, cy = 0.5f;
    steady_clock::time_point last_up = steady_clock::now();

    CameraUnit(std::string name) : id(name) {}

    void start(const std::string& src, const std::string& mid) {
        if (is_running) return;

        cap.set(cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_ANY);
        if (!cap.open(src, cv::CAP_FFMPEG)) {
            std::cerr << "[ERROR] Stream open failed: " << src << std::endl;
            return;
        }

        int fcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        std::string rp = cfg.data_root + "/raw/" + mid + "/" + id + ".mp4";
        std::string cp = cfg.data_root + "/program/" + mid + "/" + id + "_cut.mp4";

        raw_w.open(rp, fcc, 25.0, cv::Size(cap.get(3), cap.get(4)));
        cut_w.open(cp, fcc, 25.0, cv::Size(1920, 1080));

        is_running = true;
        capture_thread = std::thread(&CameraUnit::run, this);
    }

    void run() {
        cv::Mat raw;
        while (is_running) {
            if (!cap.read(raw) || raw.empty()) {
                std::this_thread::sleep_for(10ms);
                continue;
            }

            if (raw_w.isOpened()) raw_w.write(raw);

            auto now = steady_clock::now();
            if (duration_cast<milliseconds>(now - last_up).count() > 1000) {
                tx = 0.5f; ty = 0.5f;
            }
            cx += (tx - cx) * cfg.ema_alpha;
            cy += (ty - cy) * cfg.ema_alpha;

            int w = cfg.output_width, h = cfg.output_height;
            int x = std::clamp(static_cast<int>(cx * raw.cols) - w/2, 0, raw.cols - w);
            int y = std::clamp(static_cast<int>(cy * raw.rows) - h/2, 0, raw.rows - h);

            cv::Mat cut = raw(cv::Rect(x, y, w, h)).clone();

            if (cut_w.isOpened()) cut_w.write(cut);

            {
                std::lock_guard<std::mutex> lock(mtx);
                shared_frame = cut;
            }
            std::this_thread::sleep_for(1ms);
        }
    }

    void stop() {
        is_running = false;
        if (capture_thread.joinable()) capture_thread.join();
        cap.release(); raw_w.release(); cut_w.release();
    }
};

class RecordManager {
private:
    std::string mid;
    std::atomic<bool> is_run{false};
    std::thread worker;
    CameraUnit c1{"cam_01"}, c2{"cam_02"};
    std::string sel = "cam_01";
    cv::VideoWriter p_writer;
    FILE* ffmpeg_pipe = nullptr;

    // 【修正补全】：补全变量声明，解决编译报错
    steady_clock::time_point last_dec = steady_clock::now();

    const int frame_interval = 1000 / cfg.target_fps;
    long long start_timestamp_ms = 0;

public:
    RecordManager() = default;
    ~RecordManager() { stop(); }

    bool start(const std::string& m_id) {
        if (is_run) return false;
        this->mid = m_id;

        start_timestamp_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        c1.start(cfg.cam_01_src, mid);
        c2.start(cfg.cam_02_src, mid);
        std::this_thread::sleep_for(100ms);

        fs::create_directories(cfg.data_root + "/program/" + mid);
        int fcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');

        p_writer.open(cfg.data_root + "/program/" + mid + "/program.mp4", fcc, 25, cv::Size(1920, 1080));

        std::string cmd = "ffmpeg -y -f rawvideo -pixel_format bgr24 -video_size 1920x1080 -framerate 25 -i - "
                          "-c:v h264_nvenc -preset p1 -tune ull -rc:v vbr -cq 28 -g 50 -bf 0 -b:v 6M -maxrate 8M -bufsize 4M "
                          "-pix_fmt yuv420p -rtsp_transport tcp -f rtsp " + cfg.rtsp_preview_url;

        ffmpeg_pipe = _popen(cmd.c_str(), "wb");
        if (!ffmpeg_pipe) return false;

        is_run = true;
        worker = std::thread(&RecordManager::stream_loop, this);
        return true;
    }

    void stream_loop() {
        std::cout << "[MASTER] Real-time GPU Pipeline running..." << std::endl;
        auto next_frame_time = steady_clock::now();

        while (is_run) {
            next_frame_time += milliseconds(frame_interval);

            cv::Mat frame;
            if (sel == "cam_01") { std::lock_guard<std::mutex> lock(c1.mtx); frame = c1.shared_frame.clone(); }
            else { std::lock_guard<std::mutex> lock(c2.mtx); frame = c2.shared_frame.clone(); }

            if (!frame.empty()) {
                if (p_writer.isOpened()) p_writer.write(frame);
                if (ffmpeg_pipe) {
                    fwrite(frame.data, 1, frame.total() * frame.elemSize(), ffmpeg_pipe);
                    fflush(ffmpeg_pipe);
                }
            }
            std::this_thread::sleep_until(next_frame_time);
        }
    }

    void stop() {
        if (!is_run) return;
        is_run = false;
        if (worker.joinable()) worker.join();
        if (ffmpeg_pipe) { _pclose(ffmpeg_pipe); ffmpeg_pipe = nullptr; }

        long long end_timestamp_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        long long duration_sec = (end_timestamp_ms - start_timestamp_ms) / 1000;

        json idx = {
            {"match_id", mid},
            {"record_start_timestamp_ms", start_timestamp_ms},
            {"record_end_timestamp_ms", end_timestamp_ms},
            {"duration_sec", duration_sec},
            {"video_codec", "h264"},
            {"container", "mp4"},
            {"fps", 25},
            {"source_resolution", "5mp_native"},
            {"program_resolution", "1920x1080"},
            {"cam_01_raw_path", formatWindowsPath(cfg.data_root + "/raw/" + mid + "/cam_01.mp4")},
            {"cam_02_raw_path", formatWindowsPath(cfg.data_root + "/raw/" + mid + "/cam_02.mp4")},
            {"cam_01_cut_path", formatWindowsPath(cfg.data_root + "/program/" + mid + "/cam_01_cut.mp4")},
            {"cam_02_cut_path", formatWindowsPath(cfg.data_root + "/program/" + mid + "/cam_02_cut.mp4")},
            {"program_path", formatWindowsPath(cfg.data_root + "/program/" + mid + "/program.mp4")},
            {"status", "success"}
        };

        fs::create_directories(cfg.data_root + "/metadata/" + mid);
        std::ofstream f(cfg.data_root + "/metadata/" + mid + "/record_index.json");
        f << idx.dump(4);

        c1.stop(); c2.stop(); p_writer.release();
        std::cout << "[STOP] All pipelines frozen." << std::endl;
    }

    void switchP(const std::string& cid) {
        sel = cid;
        last_dec = steady_clock::now();
    }

    void updateF(const std::string& cid, float x, float y) {
        if (cid == "cam_01") { c1.tx = x; c1.ty = y; c1.last_up = steady_clock::now(); }
        else { c2.tx = x; c2.ty = y; c2.last_up = steady_clock::now(); }
    }

    std::string getSelectedCam() const { return sel; }
    bool isRunning() const { return is_run; }
};

RecordManager g_mgr;

int main() {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    loadConfig();
    httplib::Server svr;
    std::cout << "[PRO] Module B Frozen Core listening..." << std::endl;

    svr.Post("/api/v1/record/matches/init", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"code\":0,\"message\":\"ok\",\"data\":{}}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/start", [](const httplib::Request& req, httplib::Response& res) {
        bool ok = g_mgr.start(req.path_params.at("id"));
        res.set_content(ok ? "{\"code\":0,\"message\":\"ok\",\"data\":{}}" : "{\"code\":1003}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/stop", [](const httplib::Request&, httplib::Response& res) {
        g_mgr.stop();
        res.set_content("{\"code\":0,\"message\":\"ok\",\"data\":{}}", "application/json");
    });

    svr.Get("/api/v1/record/matches/:id/status", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("id");
        json response = {
            {"code", 0},
            {"message", "ok"},
            {"data", {
                         {"match_id", mid},
                         {"status", g_mgr.isRunning() ? "recording" : "idle"},
                         {"cam_01_recording", g_mgr.isRunning()},
                         {"cam_02_recording", g_mgr.isRunning()},
                         {"cam_01_cut_status", g_mgr.isRunning() ? "ready" : "idle"},
                         {"cam_02_cut_status", g_mgr.isRunning() ? "ready" : "idle"},
                         {"current_program_camera_id", g_mgr.getSelectedCam()},
                         {"preview_status", "online"},
                         {"error_message", ""}
                     }}
        };
        res.set_content(response.dump(), "application/json");
    });

    svr.Get("/api/v1/record/matches/:id/files", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("id");
        json response = {
            {"code", 0},
            {"message", "ok"},
            {"data", {
                         {"match_id", mid},
                         {"raw_files", {
                                           {{"camera_id", "cam_01"}, {"file_path", formatWindowsPath(cfg.data_root + "/raw/" + mid + "/cam_01.mp4")}},
                                           {{"camera_id", "cam_02"}, {"file_path", formatWindowsPath(cfg.data_root + "/raw/" + mid + "/cam_02.mp4")}}
                                       }},
                         {"program_files", {
                                               {{"file_path", formatWindowsPath(cfg.data_root + "/program/" + mid + "/program.mp4")}}
                                           }}
                     }}
        };
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/focus-regions", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            for (auto& region : j["regions"]) {
                float x = region["focus_region"]["x"].template get<float>() / 3840.0f;
                float y = region["focus_region"]["y"].template get<float>() / 2160.0f;
                g_mgr.updateF(region["camera_id"], x, y);
            }
            res.set_content("{\"code\":0,\"message\":\"ok\",\"data\":{}}", "application/json");
        } catch (...) { res.status = 400; }
    });

    svr.Post("/api/v1/record/matches/:id/program-decision", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            g_mgr.switchP(body["recommended_camera_id"]);
            res.set_content("{\"code\":0,\"message\":\"ok\",\"data\":{}}", "application/json");
        } catch (...) { res.status = 400; }
    });

    if (!svr.listen("0.0.0.0", cfg.server_port)) return -1;
    return 0;
}
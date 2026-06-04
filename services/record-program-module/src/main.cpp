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

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

// 吸收豆包的优雅语法
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace std::chrono;

struct AppConfig {
    int server_port = 8082;
    std::string data_root = "V:/Football_Storage";
    std::string cam01_src = "V:/Football_Storage/test_4k_source.mp4";
    std::string cam02_src = "V:/Football_Storage/test_4k_source.mp4";
    std::string rtsp_preview_url = "rtsp://127.0.0.1:8560/program";
    float ema_alpha = 0.12f; // 采用豆包的 0.12，更平滑
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
        cfg.cam01_src = j.value("cam01_src", cfg.cam01_src);
        cfg.cam02_src = j.value("cam02_src", cfg.cam02_src);
        cfg.rtsp_preview_url = j.value("rtsp_preview_url", "rtsp://127.0.0.1:8560/program");
        cfg.ema_alpha = j.value("ema_alpha", 0.12f);
        return true;
    } catch (...) { return false; }
}

class CameraUnit {
public:
    std::string id;
    cv::VideoCapture cap;
    cv::VideoWriter raw_w, cut_w; // 【守护契约】：保留本地录制
    std::atomic<bool> is_running{false};
    std::thread capture_thread;

    cv::Mat shared_frame;
    std::mutex mtx;

    float tx = 0.5f, ty = 0.5f;
    float cx = 0.5f, cy = 0.5f;
    steady_clock::time_point last_up = steady_clock::now();

    CameraUnit(std::string name) : id(name) {}

    void start(const std::string& src) {
        // 吸收豆包的硬件加速开启指令
        cap.set(cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_ANY);
        if (!cap.open(src, cv::CAP_FFMPEG)) return;
        is_running = true;
        capture_thread = std::thread(&CameraUnit::run, this);
    }

    void run() {
        cv::Mat raw;
        while (is_running) {
            if (!cap.read(raw) || raw.empty()) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                std::this_thread::sleep_for(5ms);
                continue;
            }

            // 【守护契约】：存 4K 原始流
            if (raw_w.isOpened()) raw_w.write(raw);

            auto now = steady_clock::now();
            if (duration_cast<milliseconds>(now - last_up).count() > 800) {
                tx = 0.5f; ty = 0.5f;
            }
            cx += (tx - cx) * cfg.ema_alpha;
            cy += (ty - cy) * cfg.ema_alpha;

            int w = cfg.output_width, h = cfg.output_height;
            int x = std::clamp(static_cast<int>(cx * raw.cols) - w/2, 0, raw.cols - w);
            int y = std::clamp(static_cast<int>(cy * raw.rows) - h/2, 0, raw.rows - h);

            cv::Mat cut = raw(cv::Rect(x, y, w, h)).clone();

            // 【守护契约】：存 1080P 裁切流
            if (cut_w.isOpened()) cut_w.write(cut);

            {
                std::lock_guard<std::mutex> lock(mtx);
                shared_frame = cut;
            }
            std::this_thread::sleep_for(1ms);
        }
    }

    void release() {
        is_running = false;
        if (capture_thread.joinable()) capture_thread.join();
        if (cap.isOpened()) cap.release();
        if (raw_w.isOpened()) raw_w.release();
        if (cut_w.isOpened()) cut_w.release();
    }
};

class RecordManager {
private:
    std::string mid;
    std::atomic<bool> is_run{false};
    std::thread worker;
    CameraUnit c1{"cam_01"}, c2{"cam_02"}; // 【守护契约】：双机位恢复
    std::string sel = "cam_01";
    cv::VideoWriter p_writer;
    FILE* ffmpeg_pipe = nullptr;
    steady_clock::time_point last_dec = steady_clock::now();

    const int frame_interval = 1000 / cfg.target_fps;

public:
    ~RecordManager() { stop(); }

    bool start(const std::string& match_id) {
        this->mid = match_id;
        fs::create_directories(cfg.data_root + "/raw/" + mid);
        fs::create_directories(cfg.data_root + "/program/" + mid);

        c1.start(cfg.cam01_src);
        c2.start(cfg.cam02_src);
        std::this_thread::sleep_for(100ms);

        // 【守护契约】：打开五个文件的写入句柄
        int fcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        c1.raw_w.open(cfg.data_root + "/raw/" + mid + "/cam_01.avi", fcc, 25, cv::Size(3840, 2160));
        c2.raw_w.open(cfg.data_root + "/raw/" + mid + "/cam_02.avi", fcc, 25, cv::Size(3840, 2160));
        c1.cut_w.open(cfg.data_root + "/program/" + mid + "/cam_01_cut.avi", fcc, 25, cv::Size(1920, 1080));
        c2.cut_w.open(cfg.data_root + "/program/" + mid + "/cam_02_cut.avi", fcc, 25, cv::Size(1920, 1080));
        p_writer.open(cfg.data_root + "/program/" + mid + "/program.avi", fcc, 25, cv::Size(1920, 1080));

        // 吸收豆包的超级推流命令
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
        std::cout << "[✅ W4+ 直播与录制引擎] 绝对精准帧率 + 双路全开" << std::endl;
        auto next_frame_time = steady_clock::now();

        while (is_run) {
            next_frame_time += milliseconds(frame_interval);

            if (duration_cast<milliseconds>(steady_clock::now() - last_dec).count() > 3000) sel = "cam_01";

            cv::Mat frame;
            if (sel == "cam_01") { std::lock_guard<std::mutex> lock(c1.mtx); frame = c1.shared_frame.clone(); }
            else { std::lock_guard<std::mutex> lock(c2.mtx); frame = c2.shared_frame.clone(); }

            if (!frame.empty()) {
                if (p_writer.isOpened()) p_writer.write(frame); // 【守护契约】：存主画面
                if (ffmpeg_pipe) {
                    fwrite(frame.data, 1, frame.total() * frame.elemSize(), ffmpeg_pipe);
                    fflush(ffmpeg_pipe);
                }
            }

            // 吸收豆包的神级精准休眠
            std::this_thread::sleep_until(next_frame_time);
        }
    }

    void stop() {
        if (!is_run) return;
        is_run = false;
        if (worker.joinable()) worker.join();
        if (ffmpeg_pipe) { _pclose(ffmpeg_pipe); ffmpeg_pipe = nullptr; }

        // 【守护契约】：生成交接单
        json idx = { {"match_id", mid}, {"status", "success"}, {"program_path", cfg.data_root + "/program/" + mid + "/program.avi"} };
        fs::create_directories(cfg.data_root + "/metadata/" + mid);
        std::ofstream f(cfg.data_root + "/metadata/" + mid + "/record_index.json");
        f << idx.dump(4);

        c1.release(); c2.release(); p_writer.release();
        std::cout << "[✅ 直播与录像已安全停止]" << std::endl;
    }

    void switchP(const std::string& cid) { sel = cid; last_dec = steady_clock::now(); }

    void set_focus(const std::string& cid, float x, float y) {
        if (cid == "cam_01") { c1.tx = x; c1.ty = y; c1.last_up = steady_clock::now(); }
        else { c2.tx = x; c2.ty = y; c2.last_up = steady_clock::now(); }
    }
};

RecordManager g_mgr;

int main() {
    loadConfig();
    httplib::Server svr;

    std::cout << "[🚀 模块 B 全功能启动] 端口: " << cfg.server_port << std::endl;

    svr.Post("/api/v1/record/matches/init",[](const auto&, auto& res) {
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/start",[](const auto& req, auto& res) {
        bool ok = g_mgr.start(req.path_params.at("id"));
        res.set_content(ok ? "{\"code\":0}" : "{\"code\":1003}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/program-decision",[](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body); g_mgr.switchP(body["recommended_camera_id"]);
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/stop",[](const auto&, auto& res) {
        g_mgr.stop();
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/focus-regions",[](const auto& req, auto& res) {
        try {
            auto j = json::parse(req.body);
            for (auto& region : j["regions"]) {
                float x = region["focus_region"]["x"].template get<float>() / 3840.0f;
                float y = region["focus_region"]["y"].template get<float>() / 2160.0f;
                g_mgr.set_focus(region["camera_id"], x, y);
            }
            res.set_content("{\"code\":0}", "application/json");
        } catch (...) { res.status = 400; }
    });

    svr.listen("0.0.0.0", cfg.server_port);
    return 0;
}
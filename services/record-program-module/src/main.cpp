#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ==========================================
// 全局配置结构体（从 JSON 读入）
// ==========================================
struct AppConfig {
    int server_port = 8082;
    std::string data_root = "V:/Football_Storage";
    std::string cam01_src = "";
    std::string cam02_src = "";
    float ema_alpha = 0.15f;
} cfg;

bool loadConfig() {
    std::string config_path = "config.json";
    if (!fs::exists(config_path)) {
        std::cerr << "[CONFIG] Missing config.json! Creating a default one..." << std::endl;
        // 自动创建一个默认配置（自愈能力）
        json default_json = {
            {"server_port", 8082},
            {"data_root", "V:/Football_Storage"},
            {"cam_01_src", "V:/Football_Storage/test_4k_source.mp4"},
            {"cam_02_src", "V:/Football_Storage/test_4k_source.mp4"},
            {"ema_alpha", 0.15}
        };
        std::ofstream ofs(config_path);
        ofs << default_json.dump(4);
        ofs.close();
    }

    try {
        std::ifstream ifs(config_path);
        json j = json::parse(ifs);
        cfg.server_port = j.value("server_port", 8082);
        cfg.data_root = j.value("data_root", "V:/Football_Storage");
        cfg.cam01_src = j.value("cam_01_src", "V:/Football_Storage/test_4k_source.mp4");
        cfg.cam02_src = j.value("cam_02_src", "V:/Football_Storage/test_4k_source.mp4");
        cfg.ema_alpha = j.value("ema_alpha", 0.15f);
        std::cout << "[CONFIG] Loaded from file successfully." << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[CONFIG] Parse error: " << e.what() << std::endl;
        return false;
    }
}

// ==========================================
// 逻辑类保持不变，但路径引用的地方改为使用 cfg.data_root
// ==========================================
class CameraUnit {
public:
    std::string camera_id;
    cv::VideoCapture capture;
    cv::VideoWriter raw_writer, cut_writer;
    float tx = 0.5f, ty = 0.5f, cx = 0.5f, cy = 0.5f;
    cv::Mat latest_cut_frame;
    std::mutex f_mutex;
    std::chrono::steady_clock::time_point last_up = std::chrono::steady_clock::now();

    CameraUnit(std::string id) : camera_id(id) {}

    void process() {
        cv::Mat frame;
        if (!capture.read(frame) || frame.empty()) return;
        if (raw_writer.isOpened()) raw_writer.write(frame);

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_up).count() > 1000) {
            tx = 0.5f; ty = 0.5f;
        }

        // 使用配置里的 alpha
        cx += (tx - cx) * cfg.ema_alpha;
        cy += (ty - cy) * cfg.ema_alpha;

        int cw = 1920, ch = 1080;
        int x = std::clamp(static_cast<int>(cx * frame.cols) - cw/2, 0, frame.cols - cw);
        int y = std::clamp(static_cast<int>(cy * frame.rows) - ch/2, 0, frame.rows - ch);
        cv::Mat cut = frame(cv::Rect(x, y, cw, ch)).clone();

        if (cut_writer.isOpened()) cut_writer.write(cut);
        { std::lock_guard<std::mutex> l(f_mutex); latest_cut_frame = cut; }
    }
    void release() { capture.release(); raw_writer.release(); cut_writer.release(); }
};

class RecordManager {
private:
    std::string mid;
    std::atomic<bool> is_run{false};
    std::thread worker;
    CameraUnit c1{"cam_01"}, c2{"cam_02"};
    std::string sel = "cam_01";
    cv::VideoWriter p_writer;
    std::chrono::steady_clock::time_point last_dec = std::chrono::steady_clock::now();

    void loop() {
        while (is_run) {
            c1.process(); c2.process();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - last_dec).count() > 3000) sel = "cam_01";
            cv::Mat f;
            if (sel == "cam_01") { std::lock_guard<std::mutex> l(c1.f_mutex); f = c1.latest_cut_frame.clone(); }
            else { std::lock_guard<std::mutex> l(c2.f_mutex); f = c2.latest_cut_frame.clone(); }
            if (!f.empty() && p_writer.isOpened()) p_writer.write(f);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

public:
    bool start(const std::string& m) {
        this->mid = m;
        // 使用配置里的视频源
        if (!c1.capture.open(cfg.cam01_src) || !c2.capture.open(cfg.cam02_src)) return false;

        fs::create_directories(cfg.data_root + "/raw/" + mid);
        fs::create_directories(cfg.data_root + "/program/" + mid);
        int fcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        
        c1.raw_writer.open(cfg.data_root + "/raw/" + mid + "/cam_01.avi", fcc, 25, cv::Size(c1.capture.get(3), c1.capture.get(4)));
        c2.raw_writer.open(cfg.data_root + "/raw/" + mid + "/cam_02.avi", fcc, 25, cv::Size(c2.capture.get(3), c2.capture.get(4)));
        c1.cut_writer.open(cfg.data_root + "/program/" + mid + "/cam_01_cut.avi", fcc, 25, cv::Size(1920, 1080));
        c2.cut_writer.open(cfg.data_root + "/program/" + mid + "/cam_02_cut.avi", fcc, 25, cv::Size(1920, 1080));
        p_writer.open(cfg.data_root + "/program/" + mid + "/program.avi", fcc, 25, cv::Size(1920, 1080));

        is_run = true;
        worker = std::thread(&RecordManager::loop, this);
        return true;
    }
    void stop() {
        is_run = false; if (worker.joinable()) worker.join();
        // 生成索引文件
        json idx = { {"match_id", mid}, {"status", "success"}, {"program_path", cfg.data_root + "/program/" + mid + "/program.avi"} };
        std::ofstream file(cfg.data_root + "/metadata/" + mid + "/record_index.json");
        file << idx.dump(4);
        c1.release(); c2.release(); p_writer.release();
    }
    void switchP(const std::string& cid) { sel = cid; last_dec = std::chrono::steady_clock::now(); }
    void updateF(const std::string& cid, float x, float y) { if (cid == "cam_01") { c1.tx = x; c1.ty = y; c1.last_up = std::chrono::steady_clock::now(); } else { c2.tx = x; c2.ty = y; c2.last_up = std::chrono::steady_clock::now(); } }
};

RecordManager g_mgr;

int main() {
    // 【第一步】加载配置
    if (!loadConfig()) return -1;

    httplib::Server svr;
    std::cout << "[W4+] Module B Ready with Dynamic Config." << std::endl;

    svr.Post("/api/v1/record/matches/init", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/start", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mgr.start(req.path_params.at("id"))) res.set_content("{\"code\":0}", "application/json");
        else res.set_content("{\"code\":1003}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/focus-regions", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        for (auto& r : body["regions"]) {
            float x = r["focus_region"]["x"].get<float>() / 3840.0f;
            float y = r["focus_region"]["y"].get<float>() / 2160.0f;
            g_mgr.updateF(r["camera_id"], x, y);
        }
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/program-decision", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        g_mgr.switchP(body["recommended_camera_id"]);
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/stop", [](const httplib::Request&, httplib::Response& res) {
        g_mgr.stop();
        res.set_content("{\"code\":0}", "application/json");
    });

    // 使用配置里的端口
    svr.listen("0.0.0.0", cfg.server_port);
    return 0;
}
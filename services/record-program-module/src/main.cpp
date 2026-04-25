#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <map>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// 严格遵守 v1.2 Frozen 目录契约 (建议将 V:/Football_Storage 作为你的 {data_root})
const std::string DATA_ROOT = "V:/Football_Storage";

// ==========================================
// 1. 机位单元 (CameraUnit): 增加单路裁切录制
// ==========================================
class CameraUnit {
public:
    std::string camera_id;
    cv::VideoCapture capture;
    cv::VideoWriter raw_writer;
    cv::VideoWriter cut_writer; // 【新增】单路裁切视频录制

    float target_x = 0.5f, target_y = 0.5f;
    float current_x = 0.5f, current_y = 0.5f;

    cv::Mat latest_cut_frame;
    std::mutex frame_mutex;

    // 【新增】保底机制：坐标最后更新时间
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();

    CameraUnit(std::string id) : camera_id(id) {}

    void updateFocus(float x, float y) {
        target_x = x; target_y = y;
        last_update = std::chrono::steady_clock::now();
    }

    void process() {
        cv::Mat frame;
        if (!capture.read(frame) || frame.empty()) return;

        // A. 录制原始 4K
        if (raw_writer.isOpened()) raw_writer.write(frame);

        // B. 保底逻辑：若 1 秒没收到坐标，平滑回退中心点
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() > 1000) {
            target_x = 0.5f; target_y = 0.5f;
        }

        // C. EMA 平滑裁切
        current_x += (target_x - current_x) * 0.15f;
        current_y += (target_y - current_y) * 0.15f;

        int cw = 1920, ch = 1080;
        int tx = std::clamp(static_cast<int>(current_x * frame.cols) - cw/2, 0, frame.cols - cw);
        int ty = std::clamp(static_cast<int>(current_y * frame.rows) - ch/2, 0, frame.rows - ch);
        cv::Mat cut = frame(cv::Rect(tx, ty, cw, ch)).clone();

        // D. 录制该路裁切画面 (Frozen 契约要求)
        if (cut_writer.isOpened()) cut_writer.write(cut);

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            latest_cut_frame = cut;
        }
    }

    void release() {
        capture.release(); raw_writer.release(); cut_writer.release();
    }
};

// ==========================================
// 2. 录制管理器: 增加保底决策与自动索引生成
// ==========================================
class RecordManager {
private:
    std::string match_id;
    std::atomic<bool> is_running{false};
    std::thread work_thread;

    CameraUnit cam01{"cam_01"};
    CameraUnit cam02{"cam_02"};

    std::string selected_cam = "cam_01";
    cv::VideoWriter program_writer;

    // 【新增】保底机制：决策最后更新时间
    std::chrono::steady_clock::time_point last_decision_time = std::chrono::steady_clock::now();

    void mainLoop() {
        std::cout << "[MASTER] W4 Pipeline Running..." << std::endl;
        while (is_running) {
            cam01.process();
            cam02.process();

            // A. 保底逻辑：若决策超过 3 秒没更新，强制回退主机位 cam_01
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_decision_time).count() > 3000) {
                selected_cam = "cam_01";
            }

            // B. 导播切换
            cv::Mat prog_frame;
            if (selected_cam == "cam_01") {
                std::lock_guard<std::mutex> lock(cam01.frame_mutex);
                prog_frame = cam01.latest_cut_frame.clone();
            } else {
                std::lock_guard<std::mutex> lock(cam02.frame_mutex);
                prog_frame = cam02.latest_cut_frame.clone();
            }

            if (!prog_frame.empty() && program_writer.isOpened()) {
                program_writer.write(prog_frame);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

    // 【新增】生成 record_index.json (对接 D 模块的唯一凭证)
    void generateIndexFile() {
        std::string path = DATA_ROOT + "/metadata/" + match_id + "/record_index.json";
        fs::create_directories(DATA_ROOT + "/metadata/" + match_id);

        json index = {
            {"match_id", match_id},
            {"record_end_timestamp_ms", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())},
            {"fps", 25},
            {"cam_01_raw_path", DATA_ROOT + "/raw/" + match_id + "/cam_01.avi"},
            {"cam_02_raw_path", DATA_ROOT + "/raw/" + match_id + "/cam_02.avi"},
            {"cam_01_cut_path", DATA_ROOT + "/program/" + match_id + "/cam_01_cut.avi"},
            {"cam_02_cut_path", DATA_ROOT + "/program/" + match_id + "/cam_02_cut.avi"},
            {"program_path", DATA_ROOT + "/program/" + match_id + "/program.avi"},
            {"status", "success"}
        };

        std::ofstream file(path);
        file << index.dump(4);
        std::cout << "[INDEX] Frozen record_index.json generated at: " << path << std::endl;
    }

public:
    bool start(const std::string& mid) {
        this->match_id = mid;
        std::string test_path = DATA_ROOT + "/test_4k_source.mp4";
        if (!cam01.capture.open(test_path) || !cam02.capture.open(test_path)) return false;

        fs::create_directories(DATA_ROOT + "/raw/" + mid);
        fs::create_directories(DATA_ROOT + "/program/" + mid);

        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');

        // 五路并发启动！
        cam01.raw_writer.open(DATA_ROOT + "/raw/" + mid + "/cam_01.avi", fourcc, 25, cv::Size(cam01.capture.get(3), cam01.capture.get(4)));
        cam02.raw_writer.open(DATA_ROOT + "/raw/" + mid + "/cam_02.avi", fourcc, 25, cv::Size(cam02.capture.get(3), cam02.capture.get(4)));
        cam01.cut_writer.open(DATA_ROOT + "/program/" + mid + "/cam_01_cut.avi", fourcc, 25, cv::Size(1920, 1080));
        cam02.cut_writer.open(DATA_ROOT + "/program/" + mid + "/cam_02_cut.avi", fourcc, 25, cv::Size(1920, 1080));
        program_writer.open(DATA_ROOT + "/program/" + mid + "/program.avi", fourcc, 25, cv::Size(1920, 1080));

        is_running = true;
        work_thread = std::thread(&RecordManager::mainLoop, this);
        return true;
    }

    void switchProgram(const std::string& cid) {
        selected_cam = cid;
        last_decision_time = std::chrono::steady_clock::now();
    }

    void updateCamFocus(const std::string& cid, float x, float y) {
        if (cid == "cam_01") cam01.updateFocus(x, y);
        else cam02.updateFocus(x, y);
    }

    void stop() {
        is_running = false;
        if (work_thread.joinable()) work_thread.join();
        generateIndexFile(); // 停止后立即生成交接单
        cam01.release(); cam02.release(); program_writer.release();
    }
};

RecordManager g_mgr;

// ==========================================
// 3. HTTP 接口: 增加全量 API 捕获
// ==========================================
int main() {
    httplib::Server svr;
    std::cout << "[W4] Module B Frozen Core starting..." << std::endl;

    svr.Post("/api/v1/record/matches/init", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/start", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mgr.start(req.path_params.at("id"))) res.set_content("{\"code\":0}", "application/json");
        else res.set_content("{\"code\":1003}", "application/json");
    });

    // 适配 Frozen 契约：批量坐标接口
    svr.Post("/api/v1/record/matches/:id/focus-regions", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        // 修正后的代码：使用 .get<float>() 明确告诉编译器我们要的是浮点数
        for (auto& r : body["regions"]) {
            float x_pixel = r["focus_region"]["x"].get<float>();
            float y_pixel = r["focus_region"]["y"].get<float>();
            g_mgr.updateCamFocus(r["camera_id"], x_pixel / 3840.0f, y_pixel / 2160.0f);
        }
        res.set_content("{\"code\":0}", "application/json");
    });

    // 适配 Frozen 契约：导播决策接口
    svr.Post("/api/v1/record/matches/:id/program-decision", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        g_mgr.switchProgram(body["recommended_camera_id"]);
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/stop", [](const httplib::Request& req, httplib::Response& res) {
        g_mgr.stop();
        res.set_content("{\"code\":0}", "application/json");
    });

    if (!svr.listen("0.0.0.0", 8082)) return -1;
    return 0;
}
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

const std::string STORAGE_ROOT = "V:/Football_Storage";

class RecordManager {
private:
    std::string current_match_id;
    std::atomic<bool> is_recording{false};
    std::thread record_thread;

    cv::VideoCapture capture;
    cv::VideoWriter raw_writer;
    cv::VideoWriter program_writer;

    std::mutex coord_mutex;
    float target_x_ratio = 0.5f;
    float target_y_ratio = 0.5f;
    float current_x_ratio = 0.5f;
    float current_y_ratio = 0.5f;

    void recordingLoop() {
        std::cout << "[RECORD] Thread started." << std::endl;
        cv::Mat frame;
        while (is_recording) {
            if (!capture.read(frame) || frame.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (raw_writer.isOpened()) raw_writer.write(frame);

            float tx, ty;
            {
                std::lock_guard<std::mutex> lock(coord_mutex);
                tx = target_x_ratio; ty = target_y_ratio;
            }

            // EMA 平滑
            current_x_ratio += (tx - current_x_ratio) * 0.15f;
            current_y_ratio += (ty - current_y_ratio) * 0.15f;

            int cw = 1920, ch = 1080;
            if (frame.cols >= cw && frame.rows >= ch) {
                int cx = static_cast<int>(current_x_ratio * frame.cols);
                int cy = static_cast<int>(current_y_ratio * frame.rows);
                int x = std::clamp(cx - cw/2, 0, frame.cols - cw);
                int y = std::clamp(cy - ch/2, 0, frame.rows - ch);
                cv::Mat prog = frame(cv::Rect(x, y, cw, ch));
                if (program_writer.isOpened()) program_writer.write(prog);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    }

public:
    ~RecordManager() { stopRecording(); }

    bool initMatch(const std::string& mid) {
        if (is_recording) return false;
        current_match_id = mid;
        fs::create_directories(STORAGE_ROOT + "/raw_video/" + mid);
        fs::create_directories(STORAGE_ROOT + "/program_out/" + mid);
        return true;
    }

    void updateFocus(float xr, float yr) {
        std::lock_guard<std::mutex> lock(coord_mutex);
        target_x_ratio = xr; target_y_ratio = yr;
    }

    bool start(const std::string& path) {
        if (is_recording) return false;
        if (!capture.open(path)) {
            std::cerr << "[ERR] Open failed: " << path << std::endl;
            return false;
        }
        
        std::string rp = STORAGE_ROOT + "/raw_video/" + current_match_id + "/cam_01.avi";
        std::string pp = STORAGE_ROOT + "/program_out/" + current_match_id + "/program.avi";
        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        
        raw_writer.open(rp, fourcc, 25.0, cv::Size(capture.get(3), capture.get(4)));
        program_writer.open(pp, fourcc, 25.0, cv::Size(1920, 1080));

        is_recording = true;
        record_thread = std::thread(&RecordManager::recordingLoop, this);
        return true;
    }

    void stopRecording() {
        is_recording = false;
        if (record_thread.joinable()) record_thread.join();
        capture.release();
        raw_writer.release();
        program_writer.release();
    }
};

RecordManager g_mgr;

int main() {
    httplib::Server svr;

    // 1. Init
    svr.Post("/api/v1/record/matches/init", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        g_mgr.initMatch(body["match_id"]);
        res.set_content("{\"code\":0}", "application/json");
    });

    // 2. Start (使用 :id)
    svr.Post("/api/v1/record/matches/:id/start", [](const httplib::Request& req, httplib::Response& res) {
        if (g_mgr.start("V:/Football_Storage/test_4k_source.mp4")) {
            res.set_content("{\"code\":0}", "application/json");
        } else {
            res.set_content("{\"code\":1003}", "application/json");
        }
    });

    // 3. Focus Region (【关键：补全这个路由】)
    svr.Post("/api/v1/record/matches/:id/focus-region", [](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        float xr = body["focus_region"]["x_ratio"];
        float yr = body["focus_region"]["y_ratio"];
        g_mgr.updateFocus(xr, yr);
        res.set_content("{\"code\":0}", "application/json");
    });

    // 4. Stop
    svr.Post("/api/v1/record/matches/:id/stop", [](const httplib::Request& req, httplib::Response& res) {
        g_mgr.stopRecording();
        res.set_content("{\"code\":0}", "application/json");
    });

    std::cout << "[INFO] Module B Running at 8082" << std::endl;
    svr.listen("0.0.0.0", 8082);
    return 0;
}
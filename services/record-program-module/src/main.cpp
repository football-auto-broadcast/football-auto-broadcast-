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

const std::string STORAGE_ROOT = "V:/Football_Storage";

class CameraUnit {
public:
    std::string camera_id;
    cv::VideoCapture capture;
    cv::VideoWriter raw_writer;
    
    float target_x = 0.5f, target_y = 0.5f;
    float current_x = 0.5f, current_y = 0.5f;
    
    cv::Mat latest_cut_frame;
    std::mutex frame_mutex;

    CameraUnit(std::string id) : camera_id(id) {}

    void updateFocus(float x, float y) {
        target_x = x; target_y = y;
    }

    void process() {
        cv::Mat frame;
        if (!capture.read(frame) || frame.empty()) return;

        if (raw_writer.isOpened()) raw_writer.write(frame);

        current_x += (target_x - current_x) * 0.15f;
        current_y += (target_y - current_y) * 0.15f;

        int cw = 1920, ch = 1080;
        int cx = static_cast<int>(current_x * frame.cols);
        int cy = static_cast<int>(current_y * frame.rows);
        int x = std::clamp(cx - cw/2, 0, frame.cols - cw);
        int y = std::clamp(cy - ch/2, 0, frame.rows - ch);
        
        cv::Mat cut = frame(cv::Rect(x, y, cw, ch)).clone();

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            latest_cut_frame = cut;
        }
    }
};

class RecordManager {
private:
    std::atomic<bool> is_running{false};
    std::thread work_thread;
    
    CameraUnit cam01{"cam_01"};
    CameraUnit cam02{"cam_02"};
    
    std::string selected_cam = "cam_01"; 
    cv::VideoWriter program_writer;

    void mainLoop() {
        std::cout << "[MASTER] Dual-Camera Pipeline started." << std::endl;
        int total_frames = 0; // 帧数计数器

        while (is_running) {
            cam01.process();
            cam02.process();

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

            // 每 30 帧打印一次日志，让你心里有底
            if (++total_frames % 30 == 0) {
                std::cout << "[INFO] Already recorded " << total_frames << " frames..." << std::endl;
            }
        }
    }

public:
    bool start(const std::string& mid) {
        std::string test_path = STORAGE_ROOT + "/test_4k_source.mp4";
        if (!cam01.capture.open(test_path) || !cam02.capture.open(test_path)) return false;

        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
        fs::create_directories(STORAGE_ROOT + "/raw_video/" + mid);
        fs::create_directories(STORAGE_ROOT + "/program_out/" + mid);

        cam01.raw_writer.open(STORAGE_ROOT + "/raw_video/" + mid + "/cam_01.avi", fourcc, 25.0, cv::Size(cam01.capture.get(3), cam01.capture.get(4)));
        cam02.raw_writer.open(STORAGE_ROOT + "/raw_video/" + mid + "/cam_02.avi", fourcc, 25.0, cv::Size(cam02.capture.get(3), cam02.capture.get(4)));
        program_writer.open(STORAGE_ROOT + "/program_out/" + mid + "/program.avi", fourcc, 25.0, cv::Size(1920, 1080));

        is_running = true;
        work_thread = std::thread(&RecordManager::mainLoop, this);
        return true;
    }

    void switchProgram(const std::string& cam_id) {
        std::cout << "[SWITCH] Selecting camera: " << cam_id << std::endl;
        selected_cam = cam_id;
    }

    void updateCamFocus(const std::string& cam_id, float x, float y) {
        if (cam_id == "cam_01") cam01.updateFocus(x, y);
        else cam02.updateFocus(x, y);
    }

    // 你刚才截图里的强力停止逻辑！
    void stop() {
        std::cout << "[STOP] Stopping all pipelines..." << std::endl;
        is_running = false;
        if (work_thread.joinable()) {
            work_thread.join(); 
        }
        
        if (program_writer.isOpened()) program_writer.release();
        if (cam01.raw_writer.isOpened()) cam01.raw_writer.release();
        if (cam02.raw_writer.isOpened()) cam02.raw_writer.release();
        if (cam01.capture.isOpened()) cam01.capture.release();
        if (cam02.capture.isOpened()) cam02.capture.release();
        
        std::cout << "[STOP] All files saved and handles released." << std::endl;
    }
};

RecordManager g_mgr;

int main() {
    httplib::Server svr;

    svr.Post("/api/v1/record/matches/init",[](const httplib::Request& req, httplib::Response& res) {
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/start",[](const httplib::Request& req, httplib::Response& res) {
        if (g_mgr.start(req.path_params.at("id"))) {
            res.set_content("{\"code\":0}", "application/json");
        } else {
            res.set_content("{\"code\":1003}", "application/json");
        }
    });

    svr.Post("/api/v1/record/matches/:id/program-decision",[](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        g_mgr.switchProgram(body["recommended_camera_id"]);
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/focus-regions",[](const httplib::Request& req, httplib::Response& res) {
        auto body = json::parse(req.body);
        for (auto& item : body["regions"]) {
            g_mgr.updateCamFocus(item["camera_id"], item["x_ratio"], item["y_ratio"]);
        }
        res.set_content("{\"code\":0}", "application/json");
    });

    svr.Post("/api/v1/record/matches/:id/stop",[](const httplib::Request& req, httplib::Response& res) {
        g_mgr.stop();
        res.set_content("{\"code\":0}", "application/json");
    });

    std::cout << "[W3] Dual-Core Server Running at 8082" << std::endl;
    svr.listen("0.0.0.0", 8082);
    return 0;
}
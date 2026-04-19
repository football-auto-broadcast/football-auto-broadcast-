#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

const std::string STORAGE_ROOT = "V:/Football_Storage";

bool initStorageEnvironment() {
    std::cout << "[INIT] Checking Storage: " << STORAGE_ROOT << std::endl;
    try {
        fs::create_directories(STORAGE_ROOT + "/raw_video");
        fs::create_directories(STORAGE_ROOT + "/program_out");
        fs::create_directories(STORAGE_ROOT + "/logs");

        std::string test_file = STORAGE_ROOT + "/logs/write_test.tmp";
        std::ofstream ofs(test_file);
        if (!ofs.is_open()) {
            std::cerr << "[ERROR] No Write Access to V: drive!" << std::endl;
            return false;
        }
        ofs << "test";
        ofs.close();
        fs::remove(test_file);
        std::cout << "[INIT] Storage Environment Ready." << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception during INIT: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "  Football Auto Broadcast - Module B (MVP) " << std::endl;
    std::cout << "  Status: Week 1 Service Skeleton Running  " << std::endl;
    std::cout << "===========================================" << std::endl;

    if (!initStorageEnvironment()) {
        std::cerr << "[FATAL] Storage Check Failed. Exiting." << std::endl;
        return -1;
    }

    httplib::Server svr;

    svr.Get("/api/v1/record/matches/:match_id/status", [](const httplib::Request& req, httplib::Response& res) {
        std::string match_id = req.path_params.at("match_id");
        
        json response = {
            {"code", 0},
            {"message", "ok"},
            {"data", {
                {"match_id", match_id},
                {"status", "recording"},
                {"raw_recording", true},
                {"program_recording", true},
                {"program_stream_uri", "rtsp://127.0.0.1:8560/program"}
            }}
        };
        
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(response.dump(), "application/json");
        std::cout << "[API] Status called for: " << match_id << std::endl;
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Module B Service is Running!", "text/plain");
    });

    int port = 8082;
    std::cout << "[INFO] Listening at: http://0.0.0.0:" << port << std::endl;
    
    // 如果监听失败，给个明显的报错提示
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[FATAL] PORT 8082 OCCUPIED! Please kill existing process!" << std::endl;
        return -1;
    }

    return 0;
}
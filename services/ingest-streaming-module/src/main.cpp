#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <iomanip>
#include <sstream>
#include "ingest_engine.h"
#include "httplib.h"

std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    std::cout << std::endl << "[INFO] Received signal, stopping..." << std::endl;
    g_running = false;
}

void StatusPrinter(IngestEngine& engine) {
    while (g_running) {
        auto statuses = engine.GetCameraStatuses();
        
        for (const auto& status : statuses) {
            std::cout << "[Cam" << std::setw(2) << std::setfill('0') << status.camera_id 
                      << "] " << status.width << "x" << status.height 
                      << " @ " << std::fixed << std::setprecision(1) << status.fps << "fps"
                      << " | online=" << std::boolalpha << status.online
                      << " | frames=" << status.frame_count << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void ReconnectChecker(IngestEngine& engine) {
    int offlineCount = 0;
    while (g_running) {
        auto statuses = engine.GetCameraStatuses();
        bool anyOffline = false;
        for (const auto& s : statuses) {
            if (!s.online) anyOffline = true;
        }
        
        if (anyOffline) {
            offlineCount++;
            if (offlineCount >= 2) {
                engine.CheckAndReconnect();
                offlineCount = 0;
            }
        } else {
            offlineCount = 0;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

int main() {
#ifdef _WIN32
    SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
            std::cout << std::endl << "[INFO] Received Ctrl+C, stopping..." << std::endl;
            g_running = false;
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#endif

    std::cout << "===== Ingest Streaming Service Started =====" << std::endl;

    IngestConfig config;
    config.data_root = "./data";
    config.log_level = "info";
    
    CameraConfig cam01;
    cam01.serial = "F92514845";
    cam01.camera_id = 0;
    cam01.role = "main";
    cam01.width = 1920;
    cam01.height = 1080;
    cam01.fps = 25.0;
    config.cameras.push_back(cam01);
    
    CameraConfig cam02;
    cam02.serial = "D91363830";
    cam02.camera_id = 1;
    cam02.role = "aux";
    cam02.width = 1920;
    cam02.height = 1080;
    cam02.fps = 25.0;
    config.cameras.push_back(cam02);

    IngestEngine engine;
    
    bool initOk = engine.Initialize(config);
    if (!initOk) {
        std::cout << "[WARN] IngestEngine initialize returned false" << std::endl;
    }

    bool startOk = false;
    if (initOk) {
        startOk = engine.Start();
        if (!startOk) {
            std::cout << "[WARN] IngestEngine start returned false" << std::endl;
        }
    }

    std::thread statusThread(StatusPrinter, std::ref(engine));
    std::thread reconnectThread(ReconnectChecker, std::ref(engine));

    const int httpPort = 8081;
    httplib::Server httpServer;
    
    httpServer.Get("/api/v1/ingest/status", [&engine](const httplib::Request& req, httplib::Response& res) {
        auto statuses = engine.GetCameraStatuses();
        std::string json = "{\"code\":0,\"message\":\"ok\",\"data\":{\"status\":\"";
        
        auto engineStatus = engine.GetStatus();
        if (engineStatus == IngestEngine::Status::running) {
            json += "running";
        } else if (engineStatus == IngestEngine::Status::degraded) {
            json += "degraded";
        } else if (engineStatus == IngestEngine::Status::stopped) {
            json += "stopped";
        } else if (engineStatus == IngestEngine::Status::failed) {
            json += "failed";
        } else {
            json += "idle";
        }
        
        json += "\",\"camera_count\":" + std::to_string(engine.GetTotalCameraCount()) + 
                ",\"online_count\":" + std::to_string(engine.GetOnlineCameraCount()) + 
                ",\"cameras\":[";
        
        for (size_t i = 0; i < statuses.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"camera_id\":" + std::to_string(statuses[i].camera_id) +
                    ",\"role\":\"" + statuses[i].role + "\"" +
                    ",\"online\":" + (statuses[i].online ? "true" : "false") +
                    ",\"frame_count\":" + std::to_string(statuses[i].frame_count) +
                    ",\"width\":" + std::to_string(statuses[i].width) +
                    ",\"height\":" + std::to_string(statuses[i].height);
            std::ostringstream fpsStream;
            fpsStream << std::fixed << std::setprecision(1) << statuses[i].fps;
            json += ",\"fps\":" + fpsStream.str() + "}";
        }
        
        json += "]}";
        res.set_content(json, "application/json");
    });

    std::thread httpThread([&httpServer, httpPort]() {
        std::cout << "[SUCCESS] HTTP server started on port: " << httpPort << std::endl;
        std::cout << "Access: http://127.0.0.1:" << httpPort << "/api/v1/ingest/status" << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        httpServer.listen("0.0.0.0", httpPort);
    });

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[INFO] Stopping HTTP server..." << std::endl;
    httpServer.stop();
    if (httpThread.joinable()) {
        httpThread.join();
    }

    std::cout << "[INFO] Stopping status thread..." << std::endl;
    if (statusThread.joinable()) {
        statusThread.join();
    }

    std::cout << "[INFO] Stopping reconnect thread..." << std::endl;
    if (reconnectThread.joinable()) {
        reconnectThread.join();
    }

    std::cout << "[INFO] Stopping IngestEngine..." << std::endl;
    engine.Stop();

    std::cout << "[SUCCESS] Ingest Streaming Service exited gracefully" << std::endl;
    return 0;
}
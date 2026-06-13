#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <fstream>
#include "ingest_engine.h"
#include "httplib.h"

std::atomic<bool> g_running(true);

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string val = ExtractJsonString(json, key);
    if (val.empty()) return 0;
    return std::stoi(val);
}

double ExtractJsonDouble(const std::string& json, const std::string& key) {
    std::string val = ExtractJsonString(json, key);
    if (val.empty()) return 0.0;
    return std::stod(val);
}

bool LoadConfig(const std::string& configPath, IngestConfig& config) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cout << "[WARN] Config file not found: " << configPath << ", using defaults" << std::endl;
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    config.data_root = ExtractJsonString(content, "data_root");
    if (!config.data_root.empty()) config.data_root += "/";

    size_t camPos = content.find("\"cameras\"");
    if (camPos == std::string::npos) {
        std::cout << "[WARN] No cameras section in config" << std::endl;
        return false;
    }

    size_t bracketPos = content.find('[', camPos);
    if (bracketPos == std::string::npos) return false;

    size_t pos = bracketPos + 1;
    while (true) {
        size_t objStart = content.find('{', pos);
        if (objStart == std::string::npos) break;
        size_t objEnd = content.find('}', objStart);
        if (objEnd == std::string::npos) break;

        std::string entry = content.substr(objStart, objEnd - objStart + 1);

        CameraConfig cam;
        cam.serial = ExtractJsonString(entry, "serial");
        cam.camera_id = ExtractJsonString(entry, "camera_id");
        cam.role = ExtractJsonString(entry, "role");
        cam.width = ExtractJsonInt(entry, "width");
        cam.height = ExtractJsonInt(entry, "height");
        cam.fps = ExtractJsonDouble(entry, "fps");
        cam.rtsp_url = ExtractJsonString(entry, "rtsp_url");

        if (!cam.serial.empty() && !cam.camera_id.empty()) {
            if (cam.width == 0) cam.width = 2592;
            if (cam.height == 0) cam.height = 1944;
            if (cam.fps == 0.0) cam.fps = 25.0;
            config.cameras.push_back(cam);
            std::cout << "[INFO] Config loaded: camera " << cam.camera_id
                      << " serial=" << cam.serial << " role=" << cam.role
                      << " rtsp=" << cam.rtsp_url << std::endl;
        }

        pos = objEnd + 1;
    }

    return !config.cameras.empty();
}

void SignalHandler(int signal) {
    std::cout << std::endl << "[INFO] Received signal, stopping..." << std::endl;
    g_running = false;
}

void StatusPrinter(IngestEngine& engine) {
    while (g_running) {
        auto statuses = engine.GetCameraStatuses();
        
        for (const auto& status : statuses) {
            std::cout << "[Cam " << status.camera_id 
                      << "] " << status.width << "x" << status.height 
                      << " @ " << std::fixed << std::setprecision(1) << status.fps << "fps"
                      << " | online=" << std::boolalpha << status.online
                      << " | streaming=" << std::boolalpha << status.streaming
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

static std::string GetExeDir() {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (len == 0) return ".";
    std::string s(path, len);
    size_t p = s.find_last_of("\\/");
    if (p == std::string::npos) return ".";
    return s.substr(0, p);
#else
    return ".";
#endif
}

static std::string FindConfigFile() {
    std::string exeDir = GetExeDir();
    std::vector<std::string> candidates;
    // 1) 与 exe 同目录
    candidates.push_back(exeDir + "\\config.json");
    // 2) exe 目录下 configs/ingest_streaming/
    candidates.push_back(exeDir + "\\configs\\ingest_streaming\\config.json");
    // 3) 从 exe 往上 2 级（部署在 services/xxx/bin/ 或 services/xxx/x64/Release/）
    candidates.push_back(exeDir + "\\..\\..\\configs\\ingest_streaming\\config.json");
    // 4) 从 exe 往上 3 级（services/xxx/x64/Release 或更深）
    candidates.push_back(exeDir + "\\..\\..\\..\\configs\\ingest_streaming\\config.json");
    // 5) 从 exe 往上 4 级（最深的 VS 默认输出位置）
    candidates.push_back(exeDir + "\\..\\..\\..\\..\\configs\\ingest_streaming\\config.json");
    // 6) 当前 CWD 相对路径（开发时直接运行）
    candidates.push_back("configs\\ingest_streaming\\config.json");
    // 7) 绝对路径兜底
    candidates.push_back("D:\\football-github-new\\football-auto-broadcast-\\configs\\ingest_streaming\\config.json");

    for (const auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) {
            std::cout << "[INFO] Config found at: " << c << std::endl;
            return c;
        }
    }
    std::cout << "[WARN] Config not found in any candidate path, using hardcoded defaults" << std::endl;
    return "";
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
    fprintf(stderr, "[MAIN-DEBUG] Creating IngestEngine...\n");
    fflush(stderr);

    IngestConfig config;
    std::string configPath = FindConfigFile();
    bool loaded = !configPath.empty() && LoadConfig(configPath, config);
    if (!loaded) {
        std::cout << "[WARN] Could not load config.json, using default hardcoded config" << std::endl;
        config.data_root = "./data";
        CameraConfig cam01;
        cam01.serial = "F92514845";
        cam01.camera_id = "cam_01";
        cam01.role = "main";
        cam01.width = 2592;
        cam01.height = 1944;
        cam01.fps = 25.0;
        cam01.rtsp_url = "rtsp://127.0.0.1:8554/main";
        config.cameras.push_back(cam01);
        CameraConfig cam02;
        cam02.serial = "D91363830";
        cam02.camera_id = "cam_02";
        cam02.role = "aux";
        cam02.width = 2592;
        cam02.height = 1944;
        cam02.fps = 25.0;
        cam02.rtsp_url = "rtsp://127.0.0.1:8555/aux";
        config.cameras.push_back(cam02);
    }

    IngestEngine engine;
    
    bool initOk = engine.Initialize(config);
    fprintf(stderr, "[MAIN-DEBUG] IngestEngine.Initialize returned %d\n", initOk ? 1 : 0);
    fflush(stderr);
    if (!initOk) {
        std::cout << "[WARN] IngestEngine initialize returned false" << std::endl;
    }

    bool startOk = false;
    if (initOk) {
        startOk = engine.Start();
        fprintf(stderr, "[MAIN-DEBUG] IngestEngine.Start returned %d\n", startOk ? 1 : 0);
        fflush(stderr);
        if (!startOk) {
            std::cout << "[WARN] IngestEngine start returned false" << std::endl;
        }
    }

    fprintf(stderr, "[MAIN-DEBUG] Starting threads...\n");
    fflush(stderr);
    std::thread statusThread(StatusPrinter, std::ref(engine));
    std::thread reconnectThread(ReconnectChecker, std::ref(engine));

    fprintf(stderr, "[MAIN-DEBUG] Starting HTTP server on port 8081...\n");
    fflush(stderr);
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
            json += "{\"camera_id\":\"" + statuses[i].camera_id + "\"" +
                    ",\"role\":\"" + statuses[i].role + "\"" +
                    ",\"online\":" + (statuses[i].online ? "true" : "false") +
                    ",\"streaming\":" + (statuses[i].streaming ? "true" : "false") +
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
        fprintf(stderr, "[MAIN-DEBUG] HTTP thread starting listen on port %d...\n", httpPort);
        fflush(stderr);
        std::cout << "[SUCCESS] HTTP server started on port: " << httpPort << std::endl;
        std::cout << "Access: http://127.0.0.1:" << httpPort << "/api/v1/ingest/status" << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        httpServer.listen("127.0.0.1", httpPort);
        fprintf(stderr, "[MAIN-DEBUG] HTTP thread listen returned!\n");
        fflush(stderr);
    });

    fprintf(stderr, "[MAIN-DEBUG] Entering main loop (g_running=%d)...\n", (int)g_running.load());
    fflush(stderr);

    int loopIter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        loopIter++;
        if (loopIter % 50 == 0) {  // every 5 seconds
            fprintf(stderr, "[MAIN-DEBUG] Main loop alive, iter=%d, g_running=%d\n", loopIter, (int)g_running.load());
            fflush(stderr);
        }
    }

    fprintf(stderr, "[MAIN-DEBUG] Main loop exited, g_running=%d\n", (int)g_running.load());
    fflush(stderr);

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
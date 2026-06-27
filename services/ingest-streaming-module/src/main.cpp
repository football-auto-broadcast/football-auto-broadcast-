#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <iomanip>

#include "httplib.h"
#include "ingest_engine.h"

// Global running flag for signal handling
std::atomic<bool> g_running{true};
std::atomic<bool> g_simulated{false};
std::mutex g_sim_mutex;
std::vector<FILE*> g_ffmpeg_pipes;
IngestConfig g_runtime_config;
std::string g_match_id;

static std::string JsonStringValue(const std::string& content, const std::string& key, size_t from = 0) {
    size_t pos = content.find("\"" + key + "\"", from);
    if (pos == std::string::npos) return "";
    size_t colon = content.find(':', pos);
    size_t start = content.find('"', colon + 1);
    size_t end = content.find('"', start + 1);
    if (start == std::string::npos || end == std::string::npos) return "";
    return content.substr(start + 1, end - start - 1);
}

static int JsonIntValue(const std::string& content, const std::string& key, size_t from, int fallback) {
    size_t pos = content.find("\"" + key + "\"", from);
    if (pos == std::string::npos || pos > from + 500) return fallback;
    size_t colon = content.find(':', pos);
    if (colon == std::string::npos) return fallback;
    size_t start = content.find_first_of("-0123456789", colon + 1);
    if (start == std::string::npos) return fallback;
    size_t end = content.find_first_not_of("0123456789", start + 1);
    try {
        return std::stoi(content.substr(start, end - start));
    } catch (...) {
        return fallback;
    }
}

// Load config from JSON file (simple manual parsing)
bool LoadConfig(const std::string& path, IngestConfig& config) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[ERROR] Cannot open config file: " << path << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();
    f.close();

    // Simple JSON parsing - look for key fields
    std::string sourceMode = JsonStringValue(content, "source_mode");
    if (!sourceMode.empty()) config.source_mode = sourceMode;

    // data_root
    size_t data_root_pos = content.find("\"data_root\"");
    if (data_root_pos != std::string::npos) {
        size_t colon_pos = content.find(':', data_root_pos);
        size_t start_quote = content.find('"', colon_pos);
        size_t end_quote = content.find('"', start_quote + 1);
        if (start_quote != std::string::npos && end_quote != std::string::npos) {
            config.data_root = content.substr(start_quote + 1, end_quote - start_quote - 1);
        }
    }

    // cameras array - find each camera block
    size_t cameras_pos = content.find("\"cameras\"");
    if (cameras_pos != std::string::npos) {
        size_t array_start = content.find('[', cameras_pos);
        size_t array_end = content.find(']', array_start);
        if (array_start != std::string::npos && array_end != std::string::npos) {
            std::string cameras_block = content.substr(array_start + 1, array_end - array_start - 1);

            // Split by "camera_id" to find each camera
            size_t cam_start = 0;
            while (true) {
                size_t cam_id_pos = cameras_block.find("\"camera_id\"", cam_start);
                if (cam_id_pos == std::string::npos) break;

                CameraConfig cam;
                cam.width = 1920;
                cam.height = 1080;
                cam.fps = 25.0;

                // Extract camera_id
                size_t ci_colon = cameras_block.find(':', cam_id_pos);
                size_t ci_start = cameras_block.find('"', ci_colon);
                size_t ci_end = cameras_block.find('"', ci_start + 1);
                if (ci_start != std::string::npos && ci_end != std::string::npos) {
                    cam.camera_id = cameras_block.substr(ci_start + 1, ci_end - ci_start - 1);
                }

                // Extract serial - search from beginning of this camera block
                size_t serial_pos = cameras_block.find("\"serial\"", cam_start);
                if (serial_pos != std::string::npos && serial_pos < cam_id_pos) {
                    size_t ser_colon = cameras_block.find(':', serial_pos);
                    size_t ser_start = cameras_block.find('"', ser_colon);
                    size_t ser_end = cameras_block.find('"', ser_start + 1);
                    if (ser_start != std::string::npos && ser_end != std::string::npos) {
                        cam.serial = cameras_block.substr(ser_start + 1, ser_end - ser_start - 1);
                    }
                }

                // Extract role
                size_t role_pos = cameras_block.find("\"role\"", cam_id_pos);
                if (role_pos != std::string::npos && role_pos < cam_start + 500) {
                    size_t ro_colon = cameras_block.find(':', role_pos);
                    size_t ro_start = cameras_block.find('"', ro_colon);
                    size_t ro_end = cameras_block.find('"', ro_start + 1);
                    if (ro_start != std::string::npos && ro_end != std::string::npos) {
                        cam.role = cameras_block.substr(ro_start + 1, ro_end - ro_start - 1);
                    }
                }

                // Extract rtsp_url
                size_t rtsp_pos = cameras_block.find("\"rtsp_url\"", cam_id_pos);
                if (rtsp_pos != std::string::npos && rtsp_pos < cam_start + 500) {
                    size_t rs_colon = cameras_block.find(':', rtsp_pos);
                    size_t rs_start = cameras_block.find('"', rs_colon);
                    size_t rs_end = cameras_block.find('"', rs_start + 1);
                    if (rs_start != std::string::npos && rs_end != std::string::npos) {
                        cam.rtsp_url = cameras_block.substr(rs_start + 1, rs_end - rs_start - 1);
                    }
                }

                std::string sourcePath = JsonStringValue(cameras_block, "source_path", cam_id_pos);
                if (!sourcePath.empty()) cam.source_path = sourcePath;
                cam.width = JsonIntValue(cameras_block, "width", cam_id_pos, cam.width);
                cam.height = JsonIntValue(cameras_block, "height", cam_id_pos, cam.height);

                config.cameras.push_back(cam);
                cam_start = cam_id_pos + 1;
            }
        }
    }

    std::cout << "[INFO] Loaded config: data_root=" << config.data_root
              << ", cameras=" << config.cameras.size() << std::endl;
    return config.cameras.size() > 0;
}

// HTTP status handler (Contract Section 7.3)
static void handleStatus(const httplib::Request& req, httplib::Response& res, IngestEngine& engine) {
    if (g_simulated.load()) {
        std::lock_guard<std::mutex> lock(g_sim_mutex);
        std::ostringstream resp;
        resp << "{\"code\":0,\"message\":\"ok\",\"data\":{"
             << "\"status\":\"running\","
             << "\"match_id\":\"" << g_match_id << "\","
             << "\"cam_01_status\":\"online\","
             << "\"cam_02_status\":\"online\","
             << "\"cam_01_fps\":25.0,"
             << "\"cam_02_fps\":25.0,"
             << "\"last_error\":\"\","
             << "\"cameras\":[";
        for (size_t i = 0; i < g_runtime_config.cameras.size(); ++i) {
            const auto& cam = g_runtime_config.cameras[i];
            if (i > 0) resp << ",";
            resp << "{\"camera_id\":\"" << cam.camera_id << "\","
                 << "\"role\":\"" << cam.role << "\","
                 << "\"online\":true,"
                 << "\"streaming\":true,"
                 << "\"width\":1920,"
                 << "\"height\":1080,"
                 << "\"fps\":" << std::fixed << std::setprecision(1) << cam.fps << "}";
        }
        resp << "]}}";
        res.set_content(resp.str(), "application/json");
        return;
    }
    auto statuses = engine.GetCameraStatuses();
    std::ostringstream oss;
    oss << "{\"status\":\"";
    auto engineStatus = engine.GetStatus();
    if (engineStatus == IngestEngine::Status::running) oss << "running";
    else if (engineStatus == IngestEngine::Status::degraded) oss << "degraded";
    else if (engineStatus == IngestEngine::Status::stopped) oss << "stopped";
    else if (engineStatus == IngestEngine::Status::failed) oss << "failed";
    else if (engineStatus == IngestEngine::Status::initializing) oss << "initializing";
    else oss << "idle";
    std::string cam01_status = "unknown";
    std::string cam02_status = "unknown";
    double cam01_fps = 0.0;
    double cam02_fps = 0.0;
    for (const auto& st : statuses) {
        if (st.camera_id == "cam_01") {
            cam01_status = st.online ? "online" : "offline";
            cam01_fps = st.fps;
        }
        if (st.camera_id == "cam_02") {
            cam02_status = st.online ? "online" : "offline";
            cam02_fps = st.fps;
        }
    }
    oss << "\",\"cam_01_status\":\"" << cam01_status
        << "\",\"cam_02_status\":\"" << cam02_status
        << "\",\"cam_01_fps\":" << std::fixed << std::setprecision(1) << cam01_fps
        << ",\"cam_02_fps\":" << std::fixed << std::setprecision(1) << cam02_fps
        << ",\"last_error\":\"\""
        << ",\"camera_count\":" << engine.GetTotalCameraCount()
        << ",\"online_count\":" << engine.GetOnlineCameraCount()
        << ",\"cameras\":[";
    for (size_t i = 0; i < statuses.size(); i++) {
        if (i > 0) oss << ",";
        oss << "{\"camera_id\":\"" << statuses[i].camera_id << "\""
            << ",\"role\":\"" << statuses[i].role << "\""
            << ",\"online\":" << (statuses[i].online ? "true" : "false")
            << ",\"streaming\":" << (statuses[i].streaming ? "true" : "false")
            << ",\"frame_count\":" << statuses[i].frame_count
            << ",\"width\":" << statuses[i].width
            << ",\"height\":" << statuses[i].height
            << ",\"fps\":" << std::fixed << std::setprecision(1) << statuses[i].fps << "}";
    }
    oss << "]}";
    std::string body = oss.str();

    // Wrap with makeResp per contract §7.3
    std::ostringstream resp;
    resp << "{\"code\":0,\"message\":\"ok\",\"data\":" << body << "}";
    res.set_content(resp.str(), "application/json");
}

// Unified response wrapper (Contract Section 7.3)
static std::string makeResp(int code, const std::string& msg, const std::string& body) {
    std::ostringstream oss;
    oss << "{\"code\":" << code << ",\"message\":\"" << msg << "\",\"data\":";
    if (body.empty()) oss << "{}";
    else oss << body;
    oss << "}";
    return oss.str();
}

// Contract Section 8.1: E-to-A match init
// Parses match initialization request (simplified parsing)
static void handleMatchInit(const httplib::Request& req, httplib::Response& res) {
    std::cout << "[API] POST /api/v1/ingest/matches/init body=" << req.body.substr(0, 200) << std::endl;

    std::string match_id;
    std::string content = req.body;

    // Parse match_id
    size_t mi_pos = content.find("\"match_id\"");
    if (mi_pos != std::string::npos) {
        size_t colon = content.find(':', mi_pos);
        size_t start = content.find('"', colon);
        size_t end = content.find('"', start + 1);
        if (start != std::string::npos && end != std::string::npos) {
            match_id = content.substr(start + 1, end - start - 1);
            g_match_id = match_id;
            std::cout << "[API]   match_id: " << match_id << std::endl;
        }
    }

    std::lock_guard<std::mutex> lock(g_sim_mutex);
    for (auto& cam : g_runtime_config.cameras) {
        size_t camera_pos = content.find("\"" + cam.camera_id + "\"");
        if (camera_pos != std::string::npos) {
            std::string streamUri = JsonStringValue(content, "stream_uri", camera_pos);
            if (!streamUri.empty()) cam.rtsp_url = streamUri;
        }
    }

    int camera_count = 0;
    size_t cam_start = 0;
    while (true) {
        size_t cam_pos = content.find("\"camera_id\"", cam_start);
        if (cam_pos == std::string::npos) break;
        camera_count++;
        cam_start = cam_pos + 1;
    }
    std::cout << "[API]   camera_count: " << camera_count << std::endl;

    std::string body = "{\"status\":\"initialized\",\"match_id\":\"" + match_id + "\",\"camera_count\":" + std::to_string(camera_count) + "}";
    std::string json = makeResp(0, "ok", body);
    res.set_content(json, "application/json");
}

// Contract Section 8.1: E-to-A match start
static void handleMatchStart(const httplib::Request& req, httplib::Response& res) {
    std::cout << "[API] POST /api/v1/ingest/matches/start" << std::endl;
    std::string body = "{\"status\":\"running\"}";
    std::string json = makeResp(0, "ok", body);
    res.set_content(json, "application/json");
}

// Contract Section 8.1: E-to-A match stop
static void handleMatchStop(const httplib::Request& req, httplib::Response& res) {
    std::cout << "[API] POST /api/v1/ingest/matches/stop" << std::endl;
    std::string body = "{\"status\":\"stopped\"}";
    std::string json = makeResp(0, "ok", body);
    res.set_content(json, "application/json");
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
    candidates.push_back(exeDir + "\\config.json");
    candidates.push_back(exeDir + "\\configs\\ingest_streaming\\config.json");
    candidates.push_back(exeDir + "\\..\\..\\configs\\ingest_streaming\\config.json");
    candidates.push_back(exeDir + "\\..\\..\\..\\configs\\ingest_streaming\\config.json");
    candidates.push_back(exeDir + "\\..\\..\\..\\..\\configs\\ingest_streaming\\config.json");
    candidates.push_back("configs\\ingest_streaming\\config.json");
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

static std::string Quote(const std::string& value) {
    return "\"" + value + "\"";
}

static void StopSyntheticStreams() {
    std::lock_guard<std::mutex> lock(g_sim_mutex);
    for (FILE* pipe : g_ffmpeg_pipes) {
        if (pipe) _pclose(pipe);
    }
    g_ffmpeg_pipes.clear();
}

static bool StartSyntheticStreams(const IngestConfig& config) {
    StopSyntheticStreams();
    std::lock_guard<std::mutex> lock(g_sim_mutex);
    std::string ffmpeg = GetExeDir() + "\\ffmpeg.exe";
    for (size_t i = 0; i < config.cameras.size(); ++i) {
        const auto& cam = config.cameras[i];
        std::ostringstream cmd;
        cmd << ffmpeg << " -hide_banner -loglevel warning -re ";
        if (config.source_mode == "file" && !cam.source_path.empty()) {
            cmd << "-stream_loop -1 -i " << Quote(cam.source_path) << " ";
        } else {
            const std::string color = (cam.camera_id == "cam_02") ? "testsrc2" : "testsrc";
            cmd << "-f lavfi -i " << Quote(color + "=size=1920x1080:rate=25") << " ";
        }
        cmd << "-an -vf scale=1920:1080 -c:v libx264 -preset ultrafast -tune zerolatency "
            << "-pix_fmt yuv420p -rtsp_transport tcp -f rtsp " << cam.rtsp_url;
        FILE* pipe = _popen(cmd.str().c_str(), "r");
        if (!pipe) {
            std::cout << "[ERROR] Failed to start synthetic stream for " << cam.camera_id << std::endl;
            for (FILE* existing : g_ffmpeg_pipes) {
                if (existing) _pclose(existing);
            }
            g_ffmpeg_pipes.clear();
            return false;
        }
        g_ffmpeg_pipes.push_back(pipe);
        std::cout << "[INFO] Synthetic stream started: " << cam.camera_id << " -> " << cam.rtsp_url << std::endl;
    }
    g_simulated = true;
    return true;
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
        config.data_root = "D:\\football\\data";
        CameraConfig cam01;
        cam01.serial = "F92514845";
        cam01.camera_id = "cam_01";
        cam01.role = "main";
        cam01.width = 1920;
        cam01.height = 1080;
        cam01.fps = 25.0;
        cam01.rtsp_url = "rtsp://127.0.0.1:8554/main";
        config.cameras.push_back(cam01);
        CameraConfig cam02;
        cam02.serial = "D91363830";
        cam02.camera_id = "cam_02";
        cam02.role = "aux";
        cam02.width = 1920;
        cam02.height = 1080;
        cam02.fps = 25.0;
        cam02.rtsp_url = "rtsp://127.0.0.1:8555/aux";
        config.cameras.push_back(cam02);
    }
    g_runtime_config = config;

    IngestEngine engine;
    if (config.source_mode == "camera") {
        if (!engine.Initialize(config)) {
            std::cout << "[ERROR] IngestEngine initialization failed" << std::endl;
            return 1;
        }
        if (!engine.Start()) {
            std::cout << "[WARN] IngestEngine start failed; service will stay online and keep reconnecting" << std::endl;
        }
    } else {
        if (!StartSyntheticStreams(config)) {
            std::cout << "[ERROR] Synthetic stream startup failed" << std::endl;
            return 1;
        }
    }

    const int httpPort = 8081;
    httplib::Server httpServer;

    httpServer.Post("/api/v1/ingest/matches/init", [&engine](const httplib::Request& req, httplib::Response& res) {
        handleMatchInit(req, res);
    });
    httpServer.Post("/api/v1/ingest/matches/start", [&engine](const httplib::Request& req, httplib::Response& res) {
        handleMatchStart(req, res);
    });
    httpServer.Post(R"(/api/v1/ingest/matches/([^/]+)/start)", [&engine](const httplib::Request& req, httplib::Response& res) {
        (void)engine;
        g_match_id = req.matches[1];
        if (g_simulated.load() && g_ffmpeg_pipes.empty()) {
            StartSyntheticStreams(g_runtime_config);
        }
        handleMatchStart(req, res);
    });
    httpServer.Post("/api/v1/ingest/matches/stop", [&engine](const httplib::Request& req, httplib::Response& res) {
        handleMatchStop(req, res);
    });
    httpServer.Post(R"(/api/v1/ingest/matches/([^/]+)/stop)", [&engine](const httplib::Request& req, httplib::Response& res) {
        (void)engine;
        g_match_id = req.matches[1];
        handleMatchStop(req, res);
    });
    httpServer.Get("/api/v1/ingest/status", [&engine](const httplib::Request& req, httplib::Response& res) {
        handleStatus(req, res, engine);
    });
    httpServer.Get(R"(/api/v1/ingest/matches/([^/]+)/status)", [&engine](const httplib::Request& req, httplib::Response& res) {
        (void)req;
        handleStatus(req, res, engine);
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
    std::cout << std::flush;

    while (g_running.load()) {
        if (g_simulated.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        engine.CheckAndReconnect();
        auto statuses = engine.GetCameraStatuses();
        if (statuses.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        auto engineStatus = engine.GetStatus();
        std::string statusStr = "idle";
        if (engineStatus == IngestEngine::Status::running) statusStr = "running";
        else if (engineStatus == IngestEngine::Status::degraded) statusStr = "degraded";
        else if (engineStatus == IngestEngine::Status::stopped) statusStr = "stopped";
        else if (engineStatus == IngestEngine::Status::failed) statusStr = "failed";
        else if (engineStatus == IngestEngine::Status::initializing) statusStr = "initializing";

        std::ostringstream oss;
        oss << "[Cam " << statuses[0].camera_id << "] "
            << statuses[0].width << "x" << statuses[0].height << " @ "
            << std::fixed << std::setprecision(1) << statuses[0].fps << "fps"
            << " | online=" << (statuses[0].online ? "true" : "false")
            << " | streaming=" << (statuses[0].streaming ? "true" : "false")
            << " | frames=" << statuses[0].frame_count;
        if (statuses.size() > 1) {
            oss << "\n[Cam " << statuses[1].camera_id << "] "
                << statuses[1].width << "x" << statuses[1].height << " @ "
                << std::fixed << std::setprecision(1) << statuses[1].fps << "fps"
                << " | online=" << (statuses[1].online ? "true" : "false")
                << " | streaming=" << (statuses[1].streaming ? "true" : "false")
                << " | frames=" << statuses[1].frame_count;
        }
        static std::string lastStatus;
        std::string curStatus = oss.str();
        if (curStatus != lastStatus) {
            std::cout << curStatus << std::endl;
            lastStatus = curStatus;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        static int loopCounter = 0;
        loopCounter++;
        if (loopCounter % 100 == 0) {
            fprintf(stderr, "[MAIN-DEBUG] Main loop alive, iter=%d, g_running=%d\n", loopCounter, (int)g_running.load());
            fflush(stderr);
        }
    }

    fprintf(stderr, "[MAIN-DEBUG] Exiting main loop, shutting down...\n");
    fflush(stderr);
    engine.Stop();
    StopSyntheticStreams();
    httpServer.stop();
    httpThread.join();
    std::cout << "[INFO] Ingest Streaming Service stopped." << std::endl;
    return 0;
}

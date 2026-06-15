#include "data_models.h"
#include "httplib.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <Windows.h>

// ============================================================
// Section A: Global Configuration
// ============================================================
struct AppConfig {
    int port_platform  = 8080;
    int port_ingest    = 8081;
    int port_record    = 8082;
    int port_vision    = 8083;
    int port_highlight = 8084;
    std::string data_root = "D:\\football\\data";
    std::string web_root  = "./web";
    double min_free_gb = 50.0;
    std::string rtsp_cam_01 = "rtsp://127.0.0.1:8554/main";
    std::string rtsp_cam_02 = "rtsp://127.0.0.1:8555/aux";
    std::string rtsp_program = "rtsp://127.0.0.1:8560/program";
} g_cfg;

// ---------- Simple INI Parser ----------
static void trim(std::string& s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
}

static void load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        spdlog::warn("Config file not found: {}, using defaults", path);
        return;
    }
    std::string line, section;
    while (std::getline(f, line)) {
        trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq); trim(key);
        std::string val = line.substr(eq + 1); trim(val);

        if (section == "ports") {
            if (key == "platform")  g_cfg.port_platform  = std::stoi(val);
            if (key == "ingest")    g_cfg.port_ingest    = std::stoi(val);
            if (key == "record")    g_cfg.port_record    = std::stoi(val);
            if (key == "vision")    g_cfg.port_vision    = std::stoi(val);
            if (key == "highlight") g_cfg.port_highlight = std::stoi(val);
        } else if (section == "paths") {
            if (key == "data_root") g_cfg.data_root = val;
            if (key == "web_root")  g_cfg.web_root  = val;
        } else if (section == "storage") {
            if (key == "min_free_gb") g_cfg.min_free_gb = std::stod(val);
        } else if (section == "rtsp") {
            if (key == "cam_01_stream")    g_cfg.rtsp_cam_01 = val;
            if (key == "cam_02_stream")    g_cfg.rtsp_cam_02 = val;
            if (key == "program_preview")  g_cfg.rtsp_program = val;
        }
    }
    spdlog::info("Config loaded: platform={}, ingest={}, record={}, vision={}, highlight={}, data_root={}",
        g_cfg.port_platform, g_cfg.port_ingest, g_cfg.port_record, g_cfg.port_vision, g_cfg.port_highlight, g_cfg.data_root);
}

// ============================================================
// Section B: Global State
// ============================================================
std::map<std::string, Match> g_matches;
std::mutex g_matches_mutex;

// Per-module cached status (populated by poll)
ModuleStatus g_status_a;
ModuleStatus g_status_b;
ModuleStatus g_status_c;
ModuleStatus g_status_d;
std::mutex g_status_mutex;

// ============================================================
// Section C: Helpers
// ============================================================
static std::string generate_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return prefix + std::to_string(ms);
}

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static bool check_disk_space(double min_gb) {
    ULARGE_INTEGER freeBytes;
    std::string root = g_cfg.data_root;
    // Use the drive root from data_root
    if (root.size() >= 2 && root[1] == ':') {
        root = root.substr(0, 3); // e.g. "D:\\"
    }
    if (GetDiskFreeSpaceExA(root.c_str(), &freeBytes, NULL, NULL)) {
        double free_gb = static_cast<double>(freeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        spdlog::info("Disk free space: {:.2f} GB (min required: {:.0f} GB)", free_gb, min_gb);
        return free_gb >= min_gb;
    }
    spdlog::error("Failed to query disk space for {}", root);
    return false;
}

static std::string get_log_filename() {
    auto t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
    return std::string("logs/platform_orchestration_") + buf + ".log";
}

// ============================================================
// Section D: HTTP Client — Downstream Module Calls
// ============================================================

// Build default camera list per contract Section 8.1
static std::vector<CameraEntry> build_default_cameras() {
    return {
        {"cam_01", "main", "MV-CE050-30GC", "6mm_C_mount", g_cfg.rtsp_cam_01},
        {"cam_02", "aux",  "MV-CE050-30GC", "6mm_C_mount", g_cfg.rtsp_cam_02}
    };
}

// D.1 Call Module A: Ingest Init (Contract Section 8.1)
static std::pair<int, std::string> call_ingest_init(const Match& m) {
    IngestInitRequest req;
    req.match_id = m.match_id;
    req.cameras = build_default_cameras();
    // network_config / capture_config / camera_param_strategy use defaults from struct

    std::string url = "http://127.0.0.1:" + std::to_string(g_cfg.port_ingest);
    httplib::Client cli(url);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    std::string body = json(req).dump();
    spdlog::info("[E->A] POST {}/api/v1/ingest/matches/init  match_id={}", url, m.match_id);

    auto res = cli.Post("/api/v1/ingest/matches/init", body, "application/json");
    if (!res) {
        spdlog::error("[E->A] Ingest init failed: no response from {}", url);
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Ingest module (A) unreachable"};
    }
    spdlog::info("[E->A] Response: status={} body={}", res->status, res->body);
    try {
        auto j = json::parse(res->body);
        int code = j.value("code", -1);
        return {code, j.value("message", "")};
    } catch (...) {
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Invalid response from module A"};
    }
}

// D.2 Call Module B: Record Init (Contract Section 8.2)
static std::pair<int, std::string> call_record_init(const Match& m) {
    RecordInitRequest req;
    req.match_id = m.match_id;
    req.input_streams = {
        {"cam_01", "main", g_cfg.rtsp_cam_01},
        {"cam_02", "aux",  g_cfg.rtsp_cam_02}
    };
    req.storage_config.raw_root      = g_cfg.data_root + "\\raw";
    req.storage_config.program_root  = g_cfg.data_root + "\\program";
    req.storage_config.metadata_root = g_cfg.data_root + "\\metadata";
    // program_config / record_config use defaults from struct

    std::string url = "http://127.0.0.1:" + std::to_string(g_cfg.port_record);
    httplib::Client cli(url);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    std::string body = json(req).dump();
    spdlog::info("[E->B] POST {}/api/v1/record/matches/init  match_id={}", url, m.match_id);

    auto res = cli.Post("/api/v1/record/matches/init", body, "application/json");
    if (!res) {
        spdlog::error("[E->B] Record init failed: no response from {}", url);
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Record module (B) unreachable"};
    }
    spdlog::info("[E->B] Response: status={} body={}", res->status, res->body);
    try {
        auto j = json::parse(res->body);
        int code = j.value("code", -1);
        return {code, j.value("message", "")};
    } catch (...) {
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Invalid response from module B"};
    }
}

// D.3 Call Module C: Vision Init (Contract Section 8.4)
static std::pair<int, std::string> call_vision_init(const Match& m) {
    VisionInitRequest req;
    req.match_id = m.match_id;
    req.streams = {
        {"cam_01", "main", g_cfg.rtsp_cam_01},
        {"cam_02", "aux",  g_cfg.rtsp_cam_02}
    };
    // event_config / fusion_config / default_region_policy use defaults from struct

    std::string url = "http://127.0.0.1:" + std::to_string(g_cfg.port_vision);
    httplib::Client cli(url);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    std::string body = json(req).dump();
    spdlog::info("[E->C] POST {}/api/v1/vision/matches/init  match_id={}", url, m.match_id);

    auto res = cli.Post("/api/v1/vision/matches/init", body, "application/json");
    if (!res) {
        spdlog::error("[E->C] Vision init failed: no response from {}", url);
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Vision module (C) unreachable"};
    }
    spdlog::info("[E->C] Response: status={} body={}", res->status, res->body);
    try {
        auto j = json::parse(res->body);
        int code = j.value("code", -1);
        return {code, j.value("message", "")};
    } catch (...) {
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Invalid response from module C"};
    }
}

// D.4 Call Module D: Highlight Generate (Contract Section 8.6)
static std::pair<int, std::string> call_highlight_generate(const Match& m) {
    HighlightGenerateRequest req;
    req.match_id = m.match_id;
    req.mode = "full_highlight";
    req.record_index_path     = g_cfg.data_root + "\\metadata\\" + m.match_id + "\\record_index.json";
    req.event_candidates_path = g_cfg.data_root + "\\metadata\\" + m.match_id + "\\event_candidates.json";
    // clip_policy uses contract defaults

    std::string url = "http://127.0.0.1:" + std::to_string(g_cfg.port_highlight);
    httplib::Client cli(url);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(10, 0);

    std::string path = "/api/v1/highlight/matches/" + m.match_id + "/generate";
    std::string body = json(req).dump();
    spdlog::info("[E->D] POST {}{}  match_id={}", url, path, m.match_id);

    auto res = cli.Post(path.c_str(), body, "application/json");
    if (!res) {
        spdlog::error("[E->D] Highlight generate failed: no response from {}", url);
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Highlight module (D) unreachable"};
    }
    spdlog::info("[E->D] Response: status={} body={}", res->status, res->body);
    try {
        auto j = json::parse(res->body);
        int code = j.value("code", -1);
        return {code, j.value("message", "")};
    } catch (...) {
        return {ErrorCode::UPSTREAM_UNREACHABLE, "Invalid response from module D"};
    }
}

// D.5 Fetch status from a module
static ModuleStatus fetch_module_status(const std::string& name, int port, const std::string& path) {
    ModuleStatus st;
    st.module_name = name;
    st.last_checked = now_ms();

    std::string url = "http://127.0.0.1:" + std::to_string(port);
    httplib::Client cli(url);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(3, 0);

    auto res = cli.Get(path.c_str());
    if (!res) {
        st.status = "unreachable";
        st.last_error = name + " unreachable on port " + std::to_string(port);
        return st;
    }
    try {
        auto j = json::parse(res->body);
        if (j.contains("data")) {
            auto& d = j["data"];
            // Common fields
            if (d.contains("status")) st.status = d["status"].get<std::string>();

            // Module-specific fields (Contract Section 9.2 B-module, 9.3 C-module)
            if (name == "ingest") {
                if (d.contains("cam_01_status")) st.cam_01_status = d["cam_01_status"].get<std::string>();
                if (d.contains("cam_02_status")) st.cam_02_status = d["cam_02_status"].get<std::string>();
                if (d.contains("cam_01_fps"))    st.cam_01_fps    = d["cam_01_fps"].get<double>();
                if (d.contains("cam_02_fps"))    st.cam_02_fps    = d["cam_02_fps"].get<double>();
                if (d.contains("last_error"))    st.last_error    = d["last_error"].get<std::string>();
            } else if (name == "record") {
                if (d.contains("status"))                    st.status                    = d["status"].get<std::string>();
                if (d.contains("cam_01_cut_status"))         st.cam_01_cut_status         = d["cam_01_cut_status"].get<std::string>();
                if (d.contains("cam_02_cut_status"))         st.cam_02_cut_status         = d["cam_02_cut_status"].get<std::string>();
                if (d.contains("current_program_camera_id")) st.current_program_camera_id = d["current_program_camera_id"].get<std::string>();
                if (d.contains("program_output_status"))     st.program_output_status     = d["program_output_status"].get<std::string>();
                if (d.contains("record_duration_sec"))       st.record_duration_sec       = d["record_duration_sec"].get<double>();
                if (d.contains("disk_free_gb"))              st.disk_free_gb              = d["disk_free_gb"].get<double>();
                if (d.contains("last_warning"))              st.last_warning              = d["last_warning"].get<std::string>();
            } else if (name == "vision") {
                if (d.contains("focus_region_cam_01_ready"))       st.focus_region_cam_01_ready       = d["focus_region_cam_01_ready"].get<bool>();
                if (d.contains("focus_region_cam_02_ready"))       st.focus_region_cam_02_ready       = d["focus_region_cam_02_ready"].get<bool>();
                if (d.contains("last_program_decision_camera"))    st.last_program_decision_camera    = d["last_program_decision_camera"].get<std::string>();
                if (d.contains("last_focus_region_timestamp_ms"))  st.last_focus_region_timestamp_ms  = d["last_focus_region_timestamp_ms"].get<int64_t>();
                if (d.contains("last_decision_timestamp_ms"))      st.last_decision_timestamp_ms      = d["last_decision_timestamp_ms"].get<int64_t>();
                if (d.contains("last_error"))                      st.last_error                      = d["last_error"].get<std::string>();
            } else if (name == "highlight") {
                if (d.contains("last_task_status")) st.last_task_status = d["last_task_status"].get<std::string>();
                if (d.contains("last_result_path")) st.last_result_path = d["last_result_path"].get<std::string>();
                if (d.contains("last_error"))       st.last_error       = d["last_error"].get<std::string>();
            }
        }
    } catch (const std::exception& e) {
        st.status = "parse_error";
        st.last_error = std::string("Failed to parse ") + name + " response: " + e.what();
    }
    return st;
}

// D.6 Poll all modules and update cached status
static void poll_all_modules() {
    // Fetch all 4 modules in sequence (could be parallel with threads, but keeping simple)
    auto a = fetch_module_status("ingest",    g_cfg.port_ingest,    "/api/v1/ingest/status");
    auto b = fetch_module_status("record",    g_cfg.port_record,    "/api/v1/record/status");
    auto c = fetch_module_status("vision",    g_cfg.port_vision,    "/api/v1/vision/status");
    auto d = fetch_module_status("highlight", g_cfg.port_highlight, "/api/v1/highlight/status");

    {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        g_status_a = a;
        g_status_b = b;
        g_status_c = c;
        g_status_d = d;
    }
}

// D.7 Build aggregated system status for frontend (Contract Section 3.5 - 8 required items)
static json build_system_status() {
    std::lock_guard<std::mutex> lock(g_status_mutex);

    // Also update disk free space live
    double disk_gb = 0.0;
    ULARGE_INTEGER freeBytes;
    std::string root = g_cfg.data_root;
    if (root.size() >= 2 && root[1] == ':') root = root.substr(0, 3);
    if (GetDiskFreeSpaceExA(root.c_str(), &freeBytes, NULL, NULL)) {
        disk_gb = static_cast<double>(freeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
    }

    // Collect recent errors from all modules
    json recent_errors = json::array();
    auto push_error = [&](const ModuleStatus& st) {
        if (!st.last_error.empty() && st.last_error != "null" && st.last_error != "none") {
            recent_errors.push_back({
                {"module", st.module_name},
                {"error", st.last_error},
                {"timestamp_ms", st.last_checked}
            });
        }
    };
    push_error(g_status_a);
    push_error(g_status_b);
    push_error(g_status_c);
    push_error(g_status_d);

    json data;
    // 1. cam_01 status
    data["cam_01_status"] = g_status_a.cam_01_status;
    // 2. cam_02 status
    data["cam_02_status"] = g_status_a.cam_02_status;
    // 3. Current recommended program camera
    data["current_program_camera_id"] = g_status_b.current_program_camera_id;
    // 4. Current program output status
    data["program_output_status"] = g_status_b.program_output_status;
    // 5. Dual input frame rate
    data["cam_01_fps"] = g_status_a.cam_01_fps;
    data["cam_02_fps"] = g_status_a.cam_02_fps;
    // 6. Current recording duration
    data["record_duration_sec"] = g_status_b.record_duration_sec;
    // 7. Recent errors
    data["recent_errors"] = recent_errors;
    // 8. Disk free space
    data["disk_free_gb"] = disk_gb;

    // Extra fields for rich status page
    data["ingest_status"]        = g_status_a.status;
    data["record_status"]        = g_status_b.status;
    data["vision_status"]        = g_status_c.status;
    data["highlight_status"]     = g_status_d.status;
    data["ingest_error"]         = g_status_a.last_error;
    data["record_error"]         = g_status_b.last_warning;
    data["vision_error"]         = g_status_c.last_error;
    data["highlight_error"]      = g_status_d.last_error;
    data["cam_01_cut_status"]    = g_status_b.cam_01_cut_status;
    data["cam_02_cut_status"]    = g_status_b.cam_02_cut_status;
    data["focus_region_cam_01_ready"]  = g_status_c.focus_region_cam_01_ready;
    data["focus_region_cam_02_ready"]  = g_status_c.focus_region_cam_02_ready;
    data["last_program_decision_camera"]    = g_status_c.last_program_decision_camera;
    data["last_focus_region_timestamp_ms"]  = g_status_c.last_focus_region_timestamp_ms;
    data["last_decision_timestamp_ms"]      = g_status_c.last_decision_timestamp_ms;
    data["highlight_last_task_status"] = g_status_d.last_task_status;
    data["highlight_result_path"]      = g_status_d.last_result_path;
    data["rtsp_program_preview"]       = g_cfg.rtsp_program;

    return data;
}

// ============================================================
// Section E: Background Status Poller Thread
// ============================================================
static void status_poller_thread() {
    while (true) {
        poll_all_modules();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

// ============================================================
// Section F: Main — HTTP Server
// ============================================================
int main(int argc, char* argv[]) {
    // ---- F.1 Config ----
    std::string config_path = "configs/config.ini";
    if (argc > 1) config_path = argv[1];
    load_config(config_path);

    // ---- F.2 Logging (contract Section 10) ----
    CreateDirectoryA("logs", NULL);
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(get_log_filename(), true);
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::info);
    } catch (...) {
        // fallback to stdout if file logging fails
    }
    spdlog::info("========== Platform Orchestration Service Starting ==========");
    spdlog::info("Platform port: {}, Ingest: {}, Record: {}, Vision: {}, Highlight: {}",
        g_cfg.port_platform, g_cfg.port_ingest, g_cfg.port_record, g_cfg.port_vision, g_cfg.port_highlight);
    spdlog::info("Data root: {}, Min free disk: {:.0f} GB", g_cfg.data_root, g_cfg.min_free_gb);

    // ---- F.3 Start status poller ----
    std::thread poller(status_poller_thread);
    poller.detach();

    // ---- F.4 HTTP Server ----
    httplib::Server svr;

    // Serve static web files
    svr.set_mount_point("/", g_cfg.web_root);

    // ---------- Health Check ----------
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        json data = {{"status", "ok"}, {"service", "platform_orchestration_service"}};
        res.set_content(make_response(ErrorCode::SUCCESS, data).dump(), "application/json");
    });

    // ---------- List Matches ----------
    svr.Get("/api/v1/matches", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        json j_list = json::array();
        for (auto const& [id, m] : g_matches) {
            j_list.push_back(m);
        }
        res.set_content(make_response(ErrorCode::SUCCESS, j_list).dump(), "application/json");
    });

    // ---------- Get Single Match ----------
    svr.Get("/api/v1/matches/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        if (!g_matches.count(mid)) {
            res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
            return;
        }
        res.set_content(make_response(ErrorCode::SUCCESS, json(g_matches[mid])).dump(), "application/json");
    });

    // ---------- Create Match ----------
    svr.Post("/api/v1/matches", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Match m;
            m.match_id   = generate_id("match_");
            m.match_name = body.value("match_name", "Unnamed Match");
            m.status     = MatchState::IDLE;
            m.created_at = now_ms();
            m.data_root  = g_cfg.data_root;
            m.cameras    = build_default_cameras();

            {
                std::lock_guard<std::mutex> lock(g_matches_mutex);
                g_matches[m.match_id] = m;
            }
            spdlog::info("[Match:{}] Created: \"{}\"", m.match_id, m.match_name);
            res.set_content(make_response(ErrorCode::SUCCESS, json(m)).dump(), "application/json");
        } catch (const std::exception& e) {
            spdlog::error("Create match failed: {}", e.what());
            res.set_content(make_response(ErrorCode::PARAM_ERROR).dump(), "application/json");
        }
    });

    // ---------- Start Match (Orchestration Core) ----------
    svr.Post("/api/v1/matches/([^/]+)/start", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        spdlog::info("[Match:{}] Start requested", mid);

        // 1. Verify match exists
        Match m;
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (!g_matches.count(mid)) {
                res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
                return;
            }
            m = g_matches[mid];
        }

        // 2. Check state
        if (m.status != MatchState::IDLE && m.status != MatchState::STOPPED) {
            spdlog::warn("[Match:{}] Cannot start: current state is {}", mid, m.status);
            res.set_content(make_response(ErrorCode::STATE_CONFLICT,
                json{{"current_state", m.status}}).dump(), "application/json");
            return;
        }

        // 3. Check disk space (Contract Section 5.4 requirement 4)
        if (!check_disk_space(g_cfg.min_free_gb)) {
            spdlog::error("[Match:{}] Start failed: insufficient disk space (need {:.0f} GB)", mid, g_cfg.min_free_gb);
            res.set_content(make_response(ErrorCode::DISK_SPACE_INSUFFICIENT).dump(), "application/json");
            return;
        }

        // 4. Set initializing
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            g_matches[mid].status = MatchState::INITIALIZING;
        }
        spdlog::info("[Match:{}] State -> initializing", mid);

        // 5. Orchestrate: Init A -> B -> C in sequence
        json init_results = json::object();
        bool all_ok = true;

        // 5a. Call Module A: Ingest Init
        auto [code_a, msg_a] = call_ingest_init(m);
        init_results["ingest"] = {{"code", code_a}, {"message", msg_a}};
        if (code_a != ErrorCode::SUCCESS) {
            spdlog::error("[Match:{}] Ingest init failed: code={} msg={}", mid, code_a, msg_a);
            all_ok = false;
        }

        // 5b. Call Module B: Record Init
        if (all_ok) {
            auto [code_b, msg_b] = call_record_init(m);
            init_results["record"] = {{"code", code_b}, {"message", msg_b}};
            if (code_b != ErrorCode::SUCCESS) {
                spdlog::error("[Match:{}] Record init failed: code={} msg={}", mid, code_b, msg_b);
                all_ok = false;
            }
        }

        // 5c. Call Module C: Vision Init
        if (all_ok) {
            auto [code_c, msg_c] = call_vision_init(m);
            init_results["vision"] = {{"code", code_c}, {"message", msg_c}};
            if (code_c != ErrorCode::SUCCESS) {
                spdlog::error("[Match:{}] Vision init failed: code={} msg={}", mid, code_c, msg_c);
                all_ok = false;
            }
        }

        // 6. Update final state
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (all_ok) {
                g_matches[mid].status     = MatchState::RUNNING;
                g_matches[mid].started_at = now_ms();
                spdlog::info("[Match:{}] State -> running (all modules initialized)", mid);
            } else {
                g_matches[mid].status = MatchState::FAILED;
                spdlog::error("[Match:{}] State -> failed (module init error)", mid);
            }
        }

        json resp_data;
        resp_data["match_id"]     = mid;
        resp_data["status"]       = all_ok ? MatchState::RUNNING : MatchState::FAILED;
        resp_data["init_results"] = init_results;

        int resp_code = all_ok ? ErrorCode::SUCCESS : ErrorCode::TASK_EXECUTION_FAILED;
        res.set_content(make_response(resp_code, resp_data).dump(), "application/json");
    });

    // ---------- Stop Match ----------
    svr.Post("/api/v1/matches/([^/]+)/stop", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        spdlog::info("[Match:{}] Stop requested", mid);

        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (!g_matches.count(mid)) {
                res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
                return;
            }
            g_matches[mid].status      = MatchState::STOPPED;
            g_matches[mid].finished_at = now_ms();
        }
        spdlog::info("[Match:{}] State -> stopped", mid);
        res.set_content(make_response(ErrorCode::SUCCESS, json{{"match_id", mid}, {"status", MatchState::STOPPED}}).dump(), "application/json");
    });

    // ---------- Delete Match ----------
    svr.Delete("/api/v1/matches/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        spdlog::info("[Match:{}] Delete requested", mid);

        std::lock_guard<std::mutex> lock(g_matches_mutex);
        if (!g_matches.count(mid)) {
            res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
            return;
        }
        g_matches.erase(mid);
        spdlog::info("[Match:{}] Deleted from registry", mid);
        res.set_content(make_response(ErrorCode::SUCCESS, json{{"match_id", mid}, {"deleted", true}}).dump(), "application/json");
    });

    // ---------- Trigger Highlight Generation ----------
    svr.Post("/api/v1/matches/([^/]+)/highlight", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        spdlog::info("[Match:{}] Highlight generation triggered", mid);

        Match m;
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (!g_matches.count(mid)) {
                res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
                return;
            }
            m = g_matches[mid];
        }

        // Only allow highlight for stopped/finished matches
        if (m.status != MatchState::STOPPED && m.status != MatchState::SUCCESS) {
            res.set_content(make_response(ErrorCode::STATE_CONFLICT,
                json{{"current_state", m.status}, {"hint", "Match must be stopped before generating highlight"}}
            ).dump(), "application/json");
            return;
        }

        // Call Module D
        auto [code_d, msg_d] = call_highlight_generate(m);
        if (code_d != ErrorCode::SUCCESS) {
            spdlog::error("[Match:{}] Highlight generation failed: code={} msg={}", mid, code_d, msg_d);
            res.set_content(make_response(code_d).dump(), "application/json");
            return;
        }

        // Update state to processing
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            g_matches[mid].status = MatchState::PROCESSING;
        }
        spdlog::info("[Match:{}] State -> processing (highlight generation submitted)", mid);

        json resp_data = {
            {"match_id", mid},
            {"status", MatchState::PROCESSING},
            {"highlight_output_path", g_cfg.data_root + "\\output\\" + mid + "\\full_highlight.mp4"}
        };
        res.set_content(make_response(ErrorCode::SUCCESS, resp_data).dump(), "application/json");
    });

    // ---------- System Status ----------
    svr.Get("/api/v1/system/status", [](const httplib::Request&, httplib::Response& res) {
        json data = build_system_status();
        res.set_content(make_response(ErrorCode::SUCCESS, data).dump(), "application/json");
    });

    // ---------- Download Highlight Result ----------
    svr.Get("/api/v1/matches/([^/]+)/highlight/download", [](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.matches[1];
        std::string filepath = g_cfg.data_root + "\\output\\" + mid + "\\full_highlight.mp4";

        // Check if match exists
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (!g_matches.count(mid)) {
                res.set_content(make_response(ErrorCode::RESOURCE_NOT_FOUND).dump(), "application/json");
                return;
            }
        }

        std::ifstream f(filepath, std::ios::binary);
        if (!f.is_open()) {
            res.set_content(make_response(ErrorCode::FILE_READ_FAILED,
                json{{"path", filepath}, {"hint", "Highlight file not yet generated or path incorrect"}}
            ).dump(), "application/json");
            return;
        }

        std::ostringstream oss;
        oss << f.rdbuf();
        res.set_content(oss.str(), "video/mp4");
        spdlog::info("[Match:{}] Downloading highlight: {}", mid, filepath);
    });

    // ---- F.5 Start Server ----
    spdlog::info("Platform Orchestration Service listening on http://0.0.0.0:{}", g_cfg.port_platform);
    spdlog::info("Web UI: http://localhost:{}", g_cfg.port_platform);
    spdlog::info("Health: http://localhost:{}/api/v1/health", g_cfg.port_platform);

    svr.listen("0.0.0.0", g_cfg.port_platform);

    return 0;
}

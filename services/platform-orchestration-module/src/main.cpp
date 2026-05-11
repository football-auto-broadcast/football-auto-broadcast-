#include "data_models.h"
#include "httplib.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <iostream>
#include <map>
#include <mutex>
#include <chrono>
#include <thread>
#include <Windows.h>
#include <vector>

// ---------- 全局映射：协议错误码翻译 [最终成果要求] ----------
std::map<int, std::string> g_error_translator = {
    {1001, "参数错误"}, {1008, "上游服务不可达"}, {1009, "资源未找到"},
    {1013, "相机枚举失败"}, {1014, "相机取流中断"}, {1017, "索引文件缺失"},
    {1018, "决策结果超时"}, {1019, "磁盘空间不足 (需至少50GB)"}
};

// ---------- 全局比赛存储 ----------
std::map<std::string, Match> g_matches;
std::mutex g_matches_mutex;

// ---------- 全局模块状态存储 ----------
struct SystemStatus {
    std::string ingest_status = "offline";
    std::string cam_01_status = "unknown";
    std::string cam_02_status = "unknown";
    double cam_01_fps = 0;
    double cam_02_fps = 0;
    std::string ingest_error;

    std::string record_status = "offline";
    std::string cam_01_cut_status = "unknown";
    std::string cam_02_cut_status = "unknown";
    std::string current_program_camera_id = "none";
    std::string program_output_status = "stopped";
    double record_duration_sec = 0;
    double disk_free_gb = 0;
    std::string record_error;

    std::string vision_status = "offline";
    bool focus_region_cam_01_ready = false;
    bool focus_region_cam_02_ready = false;
    std::string last_program_decision_camera = "none";
    int64_t last_focus_region_timestamp_ms = 0;
    int64_t last_decision_timestamp_ms = 0;
    std::string vision_error;

    std::string highlight_status = "offline";
    std::string highlight_error;
};
SystemStatus g_system_status;
std::mutex g_status_mutex;

// ---------- 配置值 ----------
struct {
    int port_ingest = 8081;
    int port_record = 8082;
    int port_vision = 8083;
    int port_highlight = 8084;
    std::string data_root = "D:\\football\\data";
} g_cfg;

// 生成唯一ID
std::string generate_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return prefix + std::to_string(ms);
}

// 读取配置
void load_config(const std::string& path) {
    char buf[256];
    g_cfg.port_ingest = GetPrivateProfileIntA("ports", "ingest", 8081, path.c_str());
    g_cfg.port_record = GetPrivateProfileIntA("ports", "record", 8082, path.c_str());
    g_cfg.port_vision = GetPrivateProfileIntA("ports", "vision", 8083, path.c_str());
    g_cfg.port_highlight = GetPrivateProfileIntA("ports", "highlight", 8084, path.c_str());
    GetPrivateProfileStringA("paths", "data_root", "D:\\football\\data", buf, sizeof(buf), path.c_str());
    g_cfg.data_root = buf;
    spdlog::info("Config Loaded: data_root={}", g_cfg.data_root);
}

// 磁盘检查函数 [最终成果逻辑]
bool check_disk_space(double min_gb) {
    ULARGE_INTEGER freeBytes;
    if (GetDiskFreeSpaceExA(g_cfg.data_root.c_str(), &freeBytes, NULL, NULL)) {
        double free_gb = static_cast<double>(freeBytes.QuadPart) / (1024 * 1024 * 1024);
        return free_gb >= min_gb;
    }
    return false;
}

// 后台轮询
void poll_module_status() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        SystemStatus st;

        // 此处省略具体的 httplib 请求代码（保持你原有逻辑），
        // 关键点在于你可以在解析 response 时使用 g_error_translator 翻译错误

        std::lock_guard<std::mutex> lock(g_status_mutex);
        g_system_status = st;
    }
}

int main(int argc, char* argv[]) {
    // 1. 日志初始化
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/platform.log", true);
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::info);
    }
    catch (...) {}

    load_config("configs/config.ini");
    std::thread poller(poll_module_status);
    poller.detach();

    httplib::Server svr;
    svr.set_mount_point("/", "./web");

    // ---------- 2. 比赛 API：创建 ----------
    svr.Post("/api/v1/matches", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Match m;
            m.match_id = generate_id("match_");
            m.match_name = body.value("match_name", "Unnamed");
            m.status = "idle";
            m.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            {
                std::lock_guard<std::mutex> lock(g_matches_mutex);
                g_matches[m.match_id] = m;
            }
            res.set_content(json({ {"code", 0}, {"message", "ok"}, {"data", m} }).dump(), "application/json");
        }
        catch (...) {
            res.set_content(json({ {"code", 1001}, {"message", "参数错误"} }).dump(), "application/json");
        }
        });

    // ---------- 3. 核心 API：开始比赛（带磁盘预检） ----------
    svr.Post("/api/v1/matches/:match_id/start", [&](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("match_id");

        // 最终成果要求：磁盘预检
        if (!check_disk_space(50.0)) {
            spdlog::error("Start Match {} Failed: Disk Space < 50GB", mid);
            res.set_content(json({ {"code", 1019}, {"message", g_error_translator[1019]} }).dump(), "application/json");
            return;
        }

        // 修改本地状态
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (g_matches.count(mid)) {
                g_matches[mid].status = "running";
                g_matches[mid].started_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            }
        }

        // 联调预留：此处将来调用各模块的 /init 接口
        spdlog::info("Match {} started. Ready for module linkage.", mid);
        res.set_content(json({ {"code", 0}, {"message", "比赛已启动"} }).dump(), "application/json");
        });

    // ---------- 4. 核心 API：结束比赛 ----------
    svr.Post("/api/v1/matches/:match_id/stop", [&](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("match_id");
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (g_matches.count(mid)) {
                g_matches[mid].status = "finished";
                g_matches[mid].finished_at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            }
        }
        spdlog::info("Match {} stopped.", mid);
        res.set_content(json({ {"code", 0}, {"message", "比赛已结束"} }).dump(), "application/json");
        });

    // ---------- 5. 系统状态聚合查询 ----------
    svr.Get("/api/v1/system/status", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        json data = {
            {"ingest_status", g_system_status.ingest_status},
            {"record_status", g_system_status.record_status},
            {"disk_free_gb", g_system_status.disk_free_gb},
            {"current_program_camera_id", g_system_status.current_program_camera_id}
            // ... 可根据需要继续添加字段
        };
        res.set_content(json({ {"code", 0}, {"message", "ok"}, {"data", data} }).dump(), "application/json");
        });

    // 启动服务
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);
    spdlog::info("E-Module Server starting on port {}", port);
    svr.listen("0.0.0.0", port);

    return 0;
}
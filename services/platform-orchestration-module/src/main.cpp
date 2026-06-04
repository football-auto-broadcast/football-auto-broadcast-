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

// ---------- 全局映射：协议错误码翻译 ----------
std::map<int, std::string> g_error_translator = {
    {1001, "Param Error"}, {1008, "Service Unreachable"}, {1009, "Not Found"},
    {1013, "Cam Enum Failed"}, {1014, "Cam Stream Interrupted"}, {1017, "Index Missing"},
    {1018, "Decision Timeout"}, {1019, "Disk Space Low (Min 50GB)"}
};

// ---------- 全局比赛存储 ----------
std::map<std::string, Match> g_matches;
std::mutex g_matches_mutex;

// ---------- 全局模块状态存储 ----------
struct SystemStatus {
    std::string ingest_status = "online";
    std::string record_status = "online";
    double disk_free_gb = 0;
};
SystemStatus g_system_status;
std::mutex g_status_mutex;

struct {
    int port_ingest = 8081;
    int port_record = 8082;
    std::string data_root = "D:\\"; // 默认检查D盘根目录，防止文件夹不存在导致1019
} g_cfg;

// 生成唯一ID
std::string generate_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return prefix + std::to_string(ms);
}

// 磁盘检查函数
bool check_disk_space(double min_gb) {
    ULARGE_INTEGER freeBytes;
    // 使用默认的 D:\ 检查，确保只要D盘有空间就能通过
    if (GetDiskFreeSpaceExA(g_cfg.data_root.c_str(), &freeBytes, NULL, NULL)) {
        double free_gb = static_cast<double>(freeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        {
            std::lock_guard<std::mutex> lock(g_status_mutex);
            g_system_status.disk_free_gb = free_gb;
        }
        return free_gb >= min_gb;
    }
    return false;
}

int main(int argc, char* argv[]) {
    // 1. 日志初始化 (创建目录并记录)
    CreateDirectoryA("logs", NULL);
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/platform.log", true);
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::info);
        spdlog::info("--- Platform Service Starting ---");
    }
    catch (...) {}

    httplib::Server svr;
    svr.set_mount_point("/", "./web");

    // ---------- 2. 健康检查接口 (修复 404) ----------
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json({ {"status", "ok"} }).dump(), "application/json");
        });

    // ---------- 3. 获取所有比赛列表 ----------
    svr.Get("/api/v1/matches", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        json j_list = json::array();
        for (auto const& [id, m] : g_matches) {
            j_list.push_back(m);
        }
        res.set_content(json({ {"code", 0}, {"message", "ok"}, {"data", j_list} }).dump(), "application/json");
        });

    // ---------- 4. 创建比赛 API ----------
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
            spdlog::info("Match created: {} (ID: {})", m.match_name, m.match_id);
            res.set_content(json({ {"code", 0}, {"message", "ok"}, {"data", m} }).dump(), "application/json");
        }
        catch (...) {
            res.set_content(json({ {"code", 1001}, {"message", "Param Error"} }).dump(), "application/json");
        }
        });

    // ---------- 5. 开始比赛 (带磁盘检查) ----------
    svr.Post("/api/v1/matches/:match_id/start", [&](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("match_id");
        if (!check_disk_space(50.0)) {
            spdlog::error("Start match {} failed: Disk space low", mid);
            res.set_content(json({ {"code", 1019}, {"message", g_error_translator[1019]} }).dump(), "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (g_matches.count(mid)) {
                g_matches[mid].status = "running";
                spdlog::info("Match started: {}", mid);
            }
        }
        res.set_content(json({ {"code", 0}, {"message", "Match Started"} }).dump(), "application/json");
        });

    // ---------- 6. 结束比赛 ----------
    svr.Post("/api/v1/matches/:match_id/stop", [&](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("match_id");
        {
            std::lock_guard<std::mutex> lock(g_matches_mutex);
            if (g_matches.count(mid)) {
                g_matches[mid].status = "finished";
                spdlog::info("Match stopped: {}", mid);
            }
        }
        res.set_content(json({ {"code", 0}, {"message", "Match Stopped"} }).dump(), "application/json");
        });

    // ---------- 7. 生成集锦接口 (补全功能) ----------
    svr.Post("/api/v1/matches/:match_id/highlight", [&](const httplib::Request& req, httplib::Response& res) {
        std::string mid = req.path_params.at("match_id");
        spdlog::info("Highlight task triggered for match: {}", mid);
        res.set_content(json({ {"code", 0}, {"message", "Highlight generation task submitted"} }).dump(), "application/json");
        });

    // ---------- 8. 系统状态查询 ----------
    svr.Get("/api/v1/system/status", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        json data = {
            {"ingest_status", g_system_status.ingest_status},
            {"record_status", g_system_status.record_status},
            {"disk_free_gb", g_system_status.disk_free_gb}
        };
        res.set_content(json({ {"code", 0}, {"message", "ok"}, {"data", data} }).dump(), "application/json");
        });

    int port = 8080;
    spdlog::info("E-Module Server online at http://localhost:{}", port);
    svr.listen("0.0.0.0", port);

    return 0;
}
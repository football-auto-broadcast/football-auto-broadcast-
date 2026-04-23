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

// ---------- 全局比赛存储 ----------
std::map<std::string, Match> g_matches;
std::mutex g_matches_mutex;

// ---------- 全局模块状态存储 ----------
std::map<std::string, ModuleStatus> g_module_statuses;
std::mutex g_status_mutex;

// ---------- 配置值（从 config.ini 读取） ----------
struct {
    int port_ingest = 8081;
    int port_record = 8082;
    int port_vision = 8083;
    int port_highlight = 8084;
} g_cfg;

// 生成唯一ID
std::string generate_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return prefix + std::to_string(ms);
}

// 读取配置（使用 Windows 原生 API，最简单）
void load_config(const std::string& path) {
    char buf[32];
    g_cfg.port_ingest    = GetPrivateProfileIntA("ports", "ingest", 8081, path.c_str());
    g_cfg.port_record    = GetPrivateProfileIntA("ports", "record", 8082, path.c_str());
    g_cfg.port_vision    = GetPrivateProfileIntA("ports", "vision", 8083, path.c_str());
    g_cfg.port_highlight = GetPrivateProfileIntA("ports", "highlight", 8084, path.c_str());
    spdlog::info("Config loaded: ingest={}, record={}, vision={}, highlight={}",
        g_cfg.port_ingest, g_cfg.port_record, g_cfg.port_vision, g_cfg.port_highlight);
}

// 后台轮询函数（每3秒查询一次各模块状态）
void poll_module_status() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 定义模块列表
        struct Target {
            std::string name;
            int port;
        };
        std::vector<Target> targets = {
            {"ingest",    g_cfg.port_ingest},
            {"record",    g_cfg.port_record},
            {"vision",    g_cfg.port_vision},
            {"highlight", g_cfg.port_highlight}
        };

        for (auto& t : targets) {
            ModuleStatus ms;
            ms.module_name = t.name;
            ms.last_checked = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            try {
                httplib::Client cli("http://127.0.0.1", t.port);
                cli.set_connection_timeout(1, 0);  // 1秒超时
                auto res = cli.Get("/api/v1/" + t.name + "/matches/status");
                // 尝试通用状态端点（各模块可能有自己的状态路径）
                // 为了简单，我们直接请求根路径或特定状态接口
                // 这里我们假设每个模块在 /api/v1/<模块名>/status 返回 {"status":"running"} 等
                // 但实际情况可能不同，暂时先用 / 测试连通性
                // 更简单的：ping 一下 base URL，如果返回200则认为在线
                auto health_res = cli.Get("/");
                if (health_res && health_res->status == 200) {
                    ms.status = "online";
                    ms.last_error = "";
                } else {
                    ms.status = "offline";
                    ms.last_error = "HTTP " + (health_res ? std::to_string(health_res->status) : "timeout");
                }
            } catch (...) {
                ms.status = "offline";
                ms.last_error = "connection failed";
            }

            std::lock_guard<std::mutex> lock(g_status_mutex);
            g_module_statuses[t.name] = ms;
        }
    }
}

int main(int argc, char* argv[]) {
    // 日志初始化
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/platform.log", true);
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
    } catch (...) {
        std::cout << "Log init failed" << std::endl;
        return 1;
    }

    // 加载配置
    load_config("configs/config.ini");

    // 启动后台状态轮询线程
    std::thread poller(poll_module_status);
    poller.detach();

    // HTTP 服务器
    httplib::Server svr;
    svr.set_mount_point("/", "./web");

    // 健康检查
    svr.Get("/api/v1/health", [](auto&, auto& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ---------- 比赛相关 API ----------
    svr.Post("/api/v1/matches", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Match m;
            m.match_id = generate_id("match_");
            m.match_name = body.value("match_name", "Unnamed");
            m.status = "idle";
            m.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            m.started_at = m.finished_at = 0;
            {
                std::lock_guard<std::mutex> lock(g_matches_mutex);
                g_matches[m.match_id] = m;
            }
            json resp = {{"code", 0}, {"message", "ok"}, {"data", m}};
            res.set_content(resp.dump(), "application/json");
        } catch (...) {
            json resp = {{"code", 1001}, {"message", "Param error"}};
            res.set_content(resp.dump(), "application/json");
        }
    });

    svr.Get("/api/v1/matches", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        std::vector<Match> list;
        for (auto& kv : g_matches) list.push_back(kv.second);
        json resp = {{"code", 0}, {"message", "ok"}, {"data", list}};
        res.set_content(resp.dump(), "application/json");
    });

    svr.Get("/api/v1/matches/:match_id", [&](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.path_params.at("match_id");
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        auto it = g_matches.find(id);
        if (it == g_matches.end()) {
            json resp = {{"code", 1009}, {"message", "Not found"}};
            res.set_content(resp.dump(), "application/json");
            return;
        }
        json resp = {{"code", 0}, {"message", "ok"}, {"data", it->second}};
        res.set_content(resp.dump(), "application/json");
    });

    // ---------- 新增：系统状态聚合 API ----------
    svr.Get("/api/v1/system/status", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_status_mutex);
        std::vector<ModuleStatus> list;
        for (auto& kv : g_module_statuses) list.push_back(kv.second);
        json resp = {{"code", 0}, {"message", "ok"}, {"data", list}};
        res.set_content(resp.dump(), "application/json");
    });

    // 启动服务
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);
    spdlog::info("Server starting on 0.0.0.0:{}", port);
    if (!svr.listen("0.0.0.0", port)) {
        spdlog::error("Failed to start server!");
        return 1;
    }
    return 0;
}
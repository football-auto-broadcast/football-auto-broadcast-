#include "data_models.h"
#include "httplib.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <iostream>
#include <map>
#include <mutex>
#include <chrono>

// 全局比赛存储（match_id -> Match）
std::map<std::string, Match> g_matches;
std::mutex g_matches_mutex;

// 生成唯一ID的简单函数（临时用）
std::string generate_id(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return prefix + std::to_string(ms);
}

int main(int argc, char* argv[]) {
    // 日志配置
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/platform.log", true);
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
    } catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
        return 1;
    }

    // HTTP 服务器
    httplib::Server svr;

    // 挂载静态页面
    svr.set_mount_point("/", "./web");

    // 健康检查
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ---------- 创建比赛 ----------
    svr.Post("/api/v1/matches", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string match_name = body.value("match_name", "Unnamed Match");

            Match match;
            match.match_id = generate_id("match_");
            match.match_name = match_name;
            match.status = "idle";
            match.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            match.started_at = 0;
            match.finished_at = 0;

            {
                std::lock_guard<std::mutex> lock(g_matches_mutex);
                g_matches[match.match_id] = match;
            }

            json resp = { {"code", 0}, {"message", "ok"}, {"data", match} };
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            json resp = { {"code", 1001}, {"message", std::string("Param error: ") + e.what()} };
            res.set_content(resp.dump(), "application/json");
        }
    });

    // ---------- 获取比赛列表 ----------
    svr.Get("/api/v1/matches", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        std::vector<Match> matches;
        for (const auto& kv : g_matches) {
            matches.push_back(kv.second);
        }
        json resp = { {"code", 0}, {"message", "ok"}, {"data", matches} };
        res.set_content(resp.dump(), "application/json");
    });

    // ---------- 获取单个比赛 ----------
    svr.Get("/api/v1/matches/:match_id", [&](const httplib::Request& req, httplib::Response& res) {
        std::string match_id = req.path_params.at("match_id");
        std::lock_guard<std::mutex> lock(g_matches_mutex);
        auto it = g_matches.find(match_id);
        if (it == g_matches.end()) {
            json resp = { {"code", 1009}, {"message", "Resource not found"} };
            res.set_content(resp.dump(), "application/json");
            return;
        }
        json resp = { {"code", 0}, {"message", "ok"}, {"data", it->second} };
        res.set_content(resp.dump(), "application/json");
    });

    // 启动服务
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    std::string host = "0.0.0.0";
    spdlog::info("Platform Orchestration Module starting on {}:{}", host, port);
    spdlog::info("Static files served from ./web");

    if (!svr.listen(host, port)) {
        spdlog::error("Failed to start server!");
        return 1;
    }
    return 0;
}
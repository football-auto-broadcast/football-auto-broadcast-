#include "httplib.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // 最简单的日志配置：同时输出到控制台和文件（使用默认logger）
    try {
        // 创建基础文件 sink
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/platform.log", true);
        // 创建默认的 logger，同时输出到控制台和文件
        auto logger = std::make_shared<spdlog::logger>("platform", file_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::info);
    }
    catch (const spdlog::spdlog_ex& ex) {
        std::cout << "Log init failed: " << ex.what() << std::endl;
        return 1;
    }

    // 创建 HTTP 服务器
    httplib::Server svr;

    // 挂载静态页面目录（相对于可执行文件所在目录的 web 文件夹）
    svr.set_mount_point("/", "./web");

    // 健康检查接口
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
        });

    // 从命令行参数获取端口，默认 8080
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    std::string host = "0.0.0.0";  // 容器部署必须监听所有接口
    spdlog::info("Platform Orchestration Module starting on {}:{}", host, port);
    spdlog::info("Static files served from ./web");

    if (!svr.listen(host, port)) {
        spdlog::error("Failed to start server!");
        return 1;
    }

    return 0;
}
#include "camera_manager.h"
#include "http_server.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

// ============================================================================
// 全局优雅退出标志
// ============================================================================
namespace {
    // 原子标志 —— 信号处理器只负责置 false
    std::atomic<bool> g_running{true};

    // HttpServer 指针 —— 信号处理器通过它调用 Stop()
    // 注意：信号处理器中只设置原子标志；主循环检测标志后执行清理
    ingest::HttpServer* g_server = nullptr;
} // namespace

// ============================================================================
// 信号处理器（最小化操作 —— 仅设置原子标志）
// ============================================================================
static void SignalHandler(int signum) {
    const char* name = (signum == SIGINT)  ? "SIGINT"  :
                       (signum == SIGTERM) ? "SIGTERM" : "UNKNOWN";
    // 信号安全：仅使用 signal-safe 函数
    // write() 是 signal-safe 的，但 spdlog 不是 —— 所以只用 write
    const char msg[] = "\n[signal] Received signal, shutting down...\n";
#ifdef _WIN32
    // Windows 没有 write()，用最小的操作设置标志
    (void)name;
    (void)msg;
#else
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
#endif
    g_running.store(false, std::memory_order_release);
}

// ============================================================================
// 安装信号处理器
// ============================================================================
static void InstallSignalHandlers() {
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#ifdef _WIN32
    // Windows 上 SIGBREAK 对应 Ctrl+Break
    std::signal(SIGBREAK, SignalHandler);
#endif
    spdlog::info("Signal handlers installed (SIGINT, SIGTERM)");
}

// ============================================================================
// main — 守护进程入口
// ============================================================================
int main(int argc, char* argv[]) {
    // ---- 日志初始化（控制台 + 滚动文件）----
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        "ingest_streaming_module.log", true);
    file_sink->set_level(spdlog::level::debug);

    auto logger = std::make_shared<spdlog::logger>(
        "ingest", spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);

    spdlog::info("================================================");
    spdlog::info("  ingest_streaming_service  v1.0-m2             ");
    spdlog::info("  Module A — Ingest & Streaming (HTTP Control)   ");
    spdlog::info("================================================");

    // ---- 解析命令行参数（可选端口覆盖）----
    int http_port = 8081;  // contract §4.2
    if (argc >= 2) {
        http_port = std::atoi(argv[1]);
        if (http_port <= 0 || http_port > 65535) {
            spdlog::error("Invalid port: {}. Using default 8081.", argv[1]);
            http_port = 8081;
        }
    }

    // ---- 安装信号处理器 ----
    InstallSignalHandlers();

    // ---- 创建全局管理器 ----
    ingest::CameraManager manager;

    // ---- 初始化海康 MVS SDK ----
    int nRet = manager.InitializeSDK();
    if (nRet != 0) {
        spdlog::critical("SDK initialization failed (0x{:x}). Exiting.", nRet);
        return EXIT_FAILURE;
    }

    // ---- 启动 HTTP 控制面 ----
    ingest::HttpServer http_server(manager, http_port);
    g_server = &http_server;

    if (!http_server.Start()) {
        spdlog::critical("Failed to start HTTP server on port {}. "
                         "Port may be occupied.", http_port);
        manager.FinalizeSDK();
        return EXIT_FAILURE;
    }

    spdlog::info("");
    spdlog::info("  Module A is running in daemon mode.");
    spdlog::info("  Waiting for control commands from Module E...");
    spdlog::info("  Endpoints:");
    spdlog::info("    POST /api/v1/ingest/matches/init");
    spdlog::info("    POST /api/v1/ingest/matches/start");
    spdlog::info("    POST /api/v1/ingest/matches/stop");
    spdlog::info("    GET  /api/v1/ingest/status");
    spdlog::info("");
    spdlog::info("  Press Ctrl+C to shut down gracefully.");
    spdlog::info("");

    // ====================================================================
    // 主循环：等待退出信号
    // ====================================================================
    while (g_running.load(std::memory_order_acquire)) {
        // 每秒检查一次退出标志
        // 可在此处添加定期健康检查或状态汇总日志
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // ====================================================================
    // 优雅退出序列
    // ====================================================================
    spdlog::info("");
    spdlog::info("Shutdown sequence initiated...");

    // 1. 先停 HTTP 服务（拒绝新请求）
    spdlog::info("  [1/4] Stopping HTTP server...");
    http_server.Stop();

    // 2. 停止双相机取流
    spdlog::info("  [2/4] Stopping camera grabbing...");
    manager.StopAll();

    // 3. 关闭设备 + 销毁句柄
    spdlog::info("  [3/4] Closing camera devices...");
    manager.ShutdownAll();

    // 4. 反初始化 SDK
    spdlog::info("  [4/4] Finalizing MVS SDK...");
    manager.FinalizeSDK();

    g_server = nullptr;

    spdlog::info("================================================");
    spdlog::info("  ingest_streaming_service exited cleanly.       ");
    spdlog::info("================================================");
    return EXIT_SUCCESS;
}

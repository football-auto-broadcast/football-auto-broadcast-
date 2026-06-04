/**
 * main.cpp - 服务启动入口
 *
 * 解析命令行参数、设置信号处理、初始化并启动视觉分析服务。
 */

#include "service.hpp"

#include <csignal>
#include <iostream>
#include <string>

namespace {

vision::VisionService* g_service = nullptr;

void signal_handler(int signal) {
    if (g_service) {
        std::cout << "[main] Received signal " << signal << ", shutting down..." << std::endl;
        g_service->stop();
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_path = "../config/default_config.yaml";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: vision_event_service [--config <path>]" << std::endl;
            std::cout << "  --config, -c  Path to config file (default: ../config/default_config.yaml)" << std::endl;
            return 0;
        }
    }

    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 创建并启动服务
    vision::VisionService service(config_path);
    g_service = &service;

    std::cout << "[main] Starting vision event service..." << std::endl;
    if (!service.initialize()) {
        std::cerr << "[main] Failed to initialize service" << std::endl;
        return 1;
    }

    service.run();

    std::cout << "[main] Service stopped." << std::endl;
    return 0;
}

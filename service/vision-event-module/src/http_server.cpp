/**
 * http_server.cpp - HTTP API 服务器
 *
 * 提供 RESTful 接口供其他模块调用，监听端口 8083。
 *
 * API 端点：
 *   POST /api/v1/vision/matches/init              - 初始化视觉分析任务
 *   POST /api/v1/vision/matches/{match_id}/start  - 开始视觉分析
 *   POST /api/v1/vision/matches/{match_id}/stop   - 停止视觉分析
 *   GET  /api/v1/vision/matches/{match_id}/status - 查询视觉状态
 *   GET  /api/v1/vision/matches/{match_id}/event-candidates      - 查询候选事件
 *   GET  /api/v1/vision/matches/{match_id}/focus-regions         - 查询双机位关注区域
 *   GET  /api/v1/vision/matches/{match_id}/program-decision      - 查询多机位决策结果
 */

#include "json_output.hpp"
#include "event_types.hpp"
#include "focus_region.hpp"
#include "multi_focus_region.hpp"
#include "program_decision.hpp"

#include <iostream>
#include <string>
#include <functional>
#include <map>

namespace vision {

/**
 * @brief HTTP 服务器骨架
 *
 * 当前版本使用骨架接口，实际实现可替换为任意 HTTP 框架
 * (如 cpp-httplib、Crow、Drogon 等)。
 */
class HttpServer {
public:
    using Handler = std::function<ApiResponse(const std::map<std::string, std::string>&)>;

    explicit HttpServer(int port = 8083, const std::string& host = "127.0.0.1")
        : port_(port), host_(host) {}

    /**
     * @brief 注册路由处理函数
     */
    void register_route(const std::string& method, const std::string& path, Handler handler) {
        routes_[method + " " + path] = std::move(handler);
    }

    /**
     * @brief 启动服务器
     */
    bool start() {
        std::cout << "[http_server] Starting on " << host_ << ":" << port_ << std::endl;
        // TODO: 实际启动 HTTP 服务器
        return true;
    }

    /**
     * @brief 停止服务器
     */
    void stop() {
        std::cout << "[http_server] Stopping..." << std::endl;
        // TODO: 实际停止 HTTP 服务器
    }

private:
    int port_;
    std::string host_;
    std::map<std::string, Handler> routes_;
};

/**
 * @brief 创建并配置 HTTP 服务器
 *
 * 注册所有 API 端点。
 */
HttpServer create_api_server() {
    HttpServer server(8083, "127.0.0.1");

    // TODO: 注册各端点的处理函数
    // server.register_route("POST", "/api/v1/vision/matches/init", ...);
    // server.register_route("POST", "/api/v1/vision/matches/{match_id}/start", ...);
    // ...

    return server;
}

} // namespace vision

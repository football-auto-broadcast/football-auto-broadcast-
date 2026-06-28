#ifndef INGEST_STREAMING_HTTP_SERVER_H_
#define INGEST_STREAMING_HTTP_SERVER_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// cpp-httplib — 单头文件 HTTP 库，由 CMake FetchContent 拉取
// 编译时实际路径由 CMake target_include_directories 注入
#include "httplib.h"
#include "camera_manager.h"  // 需要完整 CameraManager 定义（含 GlobalStatus 内嵌类型）

namespace ingest {

// ============================================================================
// HttpServer — 模块 A 的 HTTP REST 控制面（contract §6.1）
// ============================================================================
//
// 监听 127.0.0.1:8081，提供 4 个端点供 E 模块调度（contract §4.2 / §4.4）：
//
//   POST /api/v1/ingest/matches/init   — 传入比赛配置，初始化双相机（contract §8.1）
//   POST /api/v1/ingest/matches/start  — 开始双路取流
//   POST /api/v1/ingest/matches/stop   — 停止取流并释放设备
//   GET  /api/v1/ingest/status         — 健康检查 / 状态查询
//
// 异常隔离策略：
//   每个 HTTP handler 内部使用 try-catch 包裹全部业务逻辑。
//   任何未捕获异常只会导致当前请求返回 HTTP 500，不会导致进程崩溃。
//
// 线程模型：
//   Start() → 内部启动 std::thread 调用 httplib::Server::listen()
//   Stop()  → 从外部（信号处理线程）调用 httplib::Server::stop() + join
//
class HttpServer {
public:
    /// @param manager  全局 CameraManager 实例的引用（生命周期由 main 保证）
    /// @param port     HTTP 监听端口，默认 8081（contract §4.2）
    explicit HttpServer(CameraManager& manager, int port = 8081);
    ~HttpServer();

    // ---- 禁止拷贝 ----
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // ===================================================================
    // 生命周期
    // ===================================================================

    /// @brief 启动 HTTP 服务（非阻塞）
    /// 内部创建监听线程，调用 httplib::Server::listen("127.0.0.1", port_)
    /// @return true 启动成功，false 端口已被占用或其他错误
    bool Start();

    /// @brief 停止 HTTP 服务（线程安全，可跨线程调用）
    /// 调用 httplib::Server::stop() 并 join 监听线程
    void Stop();

    /// @brief 服务器是否正在运行
    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    // ===================================================================
    // 路由注册
    // ===================================================================

    /// @brief 注册全部 4 个 REST 端点 + 异常隔离中间层
    void SetupRoutes();

    // ===================================================================
    // 通用工具
    // ===================================================================

    /// @brief 异常隔离包装器：将 handler 包裹在 try-catch 中
    /// 任何异常被捕获后返回 HTTP 500 + 统一错误 JSON
    static void SafeHandler(
        const std::function<void(const httplib::Request&,
                                  httplib::Response&)>& handler,
        const httplib::Request& req,
        httplib::Response& res);

    /// @brief 设置 CORS 头和 JSON Content-Type
    static void SetCommonHeaders(httplib::Response& res);

    /// @brief 构建统一 JSON 错误响应（contract §7.3 通用响应格式）
    static void SendError(httplib::Response& res,
                          int http_status, int code,
                          const std::string& message);

    /// @brief 构建统一 JSON 成功响应
    static void SendOk(httplib::Response& res,
                       const std::string& json_data_body = "{}");

    /// @brief 将内部 SDK 错误码映射到 contract §7.4 标准错误码
    static int MapErrorCode(int sdk_error);

    // ===================================================================
    // 业务 Handler（逐个端点）
    // ===================================================================

    void OnInit(const httplib::Request& req, httplib::Response& res);
    void OnStart(const httplib::Request& req, httplib::Response& res);
    void OnStop(const httplib::Request& req, httplib::Response& res);
    void OnStatus(const httplib::Request& req, httplib::Response& res);

    // ===================================================================
    // JSON 序列化辅助（nlohmann/json，在 .cpp 中实现）
    // ===================================================================

    /// @brief 解析 POST /init 请求体 → IngestInitRequest
    static bool ParseInitRequest(const std::string& body,
                                 struct IngestInitRequest& out_req,
                                 std::string& out_error);

    /// @brief 将 CameraManager::GlobalStatus 序列化为 JSON 字符串
    static std::string SerializeGlobalStatus(
        const CameraManager::GlobalStatus& status);

    // ===================================================================
    // 成员变量
    // ===================================================================

    CameraManager&          manager_;
    int                     port_;
    httplib::Server         svr_;

    std::atomic<bool>       running_{false};
    std::thread             listener_thread_;
};

} // namespace ingest

#endif // INGEST_STREAMING_HTTP_SERVER_H_

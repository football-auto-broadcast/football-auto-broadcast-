/**
 * http_server.hpp - 最小 HTTP API 服务器接口
 */

#ifndef VISION_EVENT_MODULE_HTTP_SERVER_HPP
#define VISION_EVENT_MODULE_HTTP_SERVER_HPP

#include <string>

namespace vision {

class VisionService;

class HttpServer {
public:
    HttpServer(const std::string& host, int port, VisionService* service);
    ~HttpServer();

    bool start();
    void stop();
    void poll_once(int timeout_ms);
    bool is_running() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace vision

#endif // VISION_EVENT_MODULE_HTTP_SERVER_HPP

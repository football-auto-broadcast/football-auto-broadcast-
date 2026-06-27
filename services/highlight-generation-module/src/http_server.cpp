#include "../include/http_server.h"
#include "../include/json_utils.h"
#include <iostream>
#include <thread>
#include <sstream>

namespace {

std::string json_escape(const std::string& text) {
    std::ostringstream oss;
    for (char ch : text) {
        if (ch == '\\') oss << "\\\\";
        else if (ch == '"') oss << "\\\"";
        else if (ch == '\n') oss << "\\n";
        else if (ch == '\r') oss << "\\r";
        else oss << ch;
    }
    return oss.str();
}

std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    const size_t key_pos = body.find(marker);
    if (key_pos == std::string::npos) return "";
    const size_t colon = body.find(':', key_pos + marker.size());
    if (colon == std::string::npos) return "";
    const size_t first_quote = body.find('"', colon + 1);
    if (first_quote == std::string::npos) return "";
    const size_t second_quote = body.find('"', first_quote + 1);
    if (second_quote == std::string::npos) return "";
    return body.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::string extract_match_id_from_path(const std::string& path) {
    const std::string prefix = "/api/v1/highlight/matches/";
    if (path.find(prefix) != 0) return "";
    const size_t start = prefix.size();
    const size_t slash = path.find('/', start);
    if (slash == std::string::npos) return "";
    return path.substr(start, slash - start);
}

} // namespace

HttpListener::HttpListener(int listenPort) : port(listenPort), serverSocket(INVALID_SOCKET) {
    currentStatus.code = 0;
    currentStatus.status = "idle";
    currentStatus.message = "System ready";
}

HttpListener::~HttpListener() {
    if (serverSocket != INVALID_SOCKET) closesocket(serverSocket);
    WSACleanup();
}

bool HttpListener::initializeServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) return false;

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // 严格冻结本机环回网络
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) return false;
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) return false;
    
    pipeline.loadConfiguration("configs/config.ini");
    return true;
}

void HttpListener::handleClient(SOCKET client) {
    char buffer[4096] = {0};
    int bytesRecv = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (bytesRecv <= 0) { closesocket(client); return; }

    std::string req(buffer);
    std::string method = req.substr(0, req.find(" "));
    size_t pathStart = req.find(" ") + 1;
    std::string path = req.substr(pathStart, req.find(" ", pathStart) - pathStart);

    std::string responseBody = "{\"code\":1001,\"message\":\"Route not found\"}";
    std::string statusLine = "HTTP/1.1 404 Not Found\r\n";

    // 路由映射 A：响应 E 模块的创建/触发高光异步生成请求
    // 兼容规则：支持 E 模块 platform_interface_spec_v1 暴露的 RESTful 路由
    if (method == "POST" && path.find("/api/v1/highlight/matches/") == 0) {
        std::string mId = extract_match_id_from_path(path);
        lastMatchId = mId;

        size_t bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";

        std::string idxPath = extract_json_string(body, "record_index_path");
        std::string evtPath = extract_json_string(body, "event_candidates_path");
        if (idxPath.empty()) idxPath = "D:\\football\\data\\metadata\\" + mId + "\\record_index.json";
        if (evtPath.empty()) evtPath = "D:\\football\\data\\metadata\\" + mId + "\\event_candidates.json";
        auto policies = JsonUtils::parseClipPolicies(body);

        // 状态转置处理
        currentStatus.status = "processing";
        currentStatus.message = "FFmpeg Worker thread spawned";
        
        // 开启高性能后台剪辑线程，完成非阻塞 202 吞吐
        std::thread worker([this, mId, idxPath, evtPath, policies]() {
            std::string outPath;
            int ret = pipeline.executeWorkflow(mId, idxPath, evtPath, policies, 180.0, outPath);
            if (ret == 0) {
                currentStatus.code = 0;
                currentStatus.status = "success";
                currentStatus.message = "Highlight aggregation process closed successfully";
                currentStatus.output_path = outPath;
            } else {
                currentStatus.code = ret;
                currentStatus.status = "failed";
                currentStatus.message = "Pipeline collapsed inside FFmpeg processing execution framework";
            }
        });
        worker.detach();

        statusLine = "HTTP/1.1 202 Accepted\r\n";
        responseBody = JsonUtils::makeStatusResponse(0, "Task submission acknowledged.", "processing", "");
    }
    // 路由映射 B：支持 2.4 / 7.5 查询状态接口轮询响应
    else if ((method == "GET" && path == "/api/v1/highlight/status") ||
             (method == "GET" && path.find("/api/v1/highlight/tasks/") == 0)) {
        statusLine = "HTTP/1.1 200 OK\r\n";
        std::stringstream body;
        body << "{\n"
             << "  \"code\": " << currentStatus.code << ",\n"
             << "  \"message\": \"" << json_escape(currentStatus.message) << "\",\n"
             << "  \"data\": {\n"
             << "    \"status\": \"" << json_escape(currentStatus.status) << "\",\n"
             << "    \"last_task_status\": \"" << json_escape(currentStatus.status) << "\",\n"
             << "    \"last_result_path\": \"" << json_escape(currentStatus.output_path) << "\",\n"
             << "    \"output_path\": \"" << json_escape(currentStatus.output_path) << "\",\n"
             << "    \"last_error\": \"" << json_escape(currentStatus.code == 0 ? "" : currentStatus.message) << "\"\n"
             << "  }\n"
             << "}";
        responseBody = body.str();
    }

    std::stringstream resp;
    resp << statusLine << "Content-Type: application/json\r\n" << "Content-Length: " << responseBody.length() << "\r\n\r\n" << responseBody;
    send(client, resp.str().c_str(), static_cast<int>(resp.str().length()), 0);
    closesocket(client);
}

void HttpListener::startListenLoop() {
    std::cout << "[Module D Server] Listening on http://127.0.0.1:" << port << " running state machine..." << std::endl;
    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(&HttpListener::handleClient, this, clientSocket).detach();
        }
    }
}

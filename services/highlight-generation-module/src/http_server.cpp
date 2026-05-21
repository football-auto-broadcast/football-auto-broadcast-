#include "../include/http_server.h"
#include "../include/json_utils.h"
#include <iostream>
#include <thread>
#include <sstream>

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
    if (method == "POST" && path.find("/api/v1/highlight/matches/") != std::string::npos) {
        size_t idPos = std::string("/api/v1/highlight/matches/").length();
        size_t nextSlash = path.find("/", idPos);
        std::string mId = path.substr(idPos, nextSlash - idPos);
        lastMatchId = mId;

        size_t bodyPos = req.find("\r\n\r\n");
        std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";

        // 默认映射全组共享网盘约定路径 (解耦规约定义)
        std::string idxPath = "D:\\football\\data\\metadata\\" + mId + "\\record_index.json";
        std::string evtPath = "D:\\football\\data\\metadata\\" + mId + "\\event_candidates.json";
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
    else if (method == "GET" && path.find("/api/v1/highlight/tasks/") != std::string::npos) {
        statusLine = "HTTP/1.1 200 OK\r\n";
        responseBody = JsonUtils::makeStatusResponse(currentStatus.code, currentStatus.message, currentStatus.status, currentStatus.output_path);
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
#include "../include/http_server.h"
#include <iostream>

int main() {
    // 固化绑定 8084 端口 (对接平台网络契约 4.2 冻结款)
    HttpListener server(8084);
    if (!server.initializeServer()) {
        std::cerr << "[CRITICAL] Port 8084 bind failed. Winsock Layer exception!" << std::endl;
        return 1011;
    }
    server.startListenLoop();
    return 0;
}
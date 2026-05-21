#include "pipeline.h"
#include <string>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

class HttpListener {
private:
    SOCKET serverSocket;
    int port;
    HighlightPipeline pipeline;
    TaskStatus currentStatus;
    std::string lastMatchId;
    void handleClient(SOCKET clientSocket);
public:
    HttpListener(int listenPort);
    ~HttpListener();
    bool initializeServer();
    void startListenLoop();
};
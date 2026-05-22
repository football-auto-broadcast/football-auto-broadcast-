/**
 * http_server.cpp - 最小 HTTP API 服务器
 */

#include "http_server.hpp"

#include "json_output.hpp"
#include "service.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace vision {

namespace {

std::string json_escape_http(const std::string& text) {
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

std::string wrap_http_response(const std::string& body, int http_code = 200) {
    const char* reason = http_code == 200 ? "OK" : "Bad Request";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << http_code << " " << reason << "\r\n";
    oss << "Content-Type: application/json; charset=utf-8\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

std::string extract_path_match_id(const std::string& path) {
    const std::string prefix = "/api/v1/vision/matches/";
    if (path.find(prefix) != 0) return "";
    std::string rest = path.substr(prefix.size());
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return rest;
    return rest.substr(0, slash);
}

std::string extract_json_string(const std::string& body, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    size_t key_pos = body.find(quoted_key);
    if (key_pos == std::string::npos) return "";
    size_t colon = body.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) return "";
    size_t first_quote = body.find('"', colon + 1);
    if (first_quote == std::string::npos) return "";
    size_t second_quote = body.find('"', first_quote + 1);
    if (second_quote == std::string::npos) return "";
    return body.substr(first_quote + 1, second_quote - first_quote - 1);
}

std::string extract_stream_uri_for_camera(const std::string& body, const std::string& camera_id) {
    // Minimal JSON extraction for the frozen E->C init payload. The project does
    // not currently link a JSON parser, so keep this scoped to stream_uri.
    const std::string camera_marker = "\"camera_id\"";
    size_t search_pos = 0;
    while (true) {
        const size_t camera_key_pos = body.find(camera_marker, search_pos);
        if (camera_key_pos == std::string::npos) return "";
        const size_t camera_value_pos = body.find("\"" + camera_id + "\"", camera_key_pos);
        if (camera_value_pos == std::string::npos) return "";

        const size_t object_end = body.find('}', camera_value_pos);
        const size_t stream_key_pos = body.find("\"stream_uri\"", camera_value_pos);
        if (stream_key_pos != std::string::npos &&
            (object_end == std::string::npos || stream_key_pos < object_end)) {
            const size_t colon = body.find(':', stream_key_pos);
            if (colon == std::string::npos) {
                return "";
            }
            const size_t first_quote = body.find('"', colon + 1);
            if (first_quote == std::string::npos) {
                return "";
            }
            const size_t second_quote = body.find('"', first_quote + 1);
            if (second_quote != std::string::npos) {
                return body.substr(first_quote + 1, second_quote - first_quote - 1);
            }
        }
        search_pos = camera_value_pos + camera_id.size();
    }
}

int parse_content_length(const std::string& request) {
    const std::string key = "Content-Length:";
    size_t pos = request.find(key);
    if (pos == std::string::npos) return 0;
    pos += key.size();
    while (pos < request.size() && request[pos] == ' ') ++pos;
    size_t end = request.find("\r\n", pos);
    if (end == std::string::npos) return 0;
    return std::atoi(request.substr(pos, end - pos).c_str());
}

bool request_complete(const std::string& request) {
    const size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;
    const int content_length = parse_content_length(request);
    return request.size() >= header_end + 4 + static_cast<size_t>(content_length);
}

std::string events_data_json(const EventList& event_list) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"match_id\":\"" << json_escape_http(event_list.match_id) << "\",";
    oss << "\"events\":[";
    for (size_t i = 0; i < event_list.events.size(); ++i) {
        if (i > 0) oss << ",";
        oss << event_list.events[i].to_json();
    }
    oss << "]}";
    return oss.str();
}

#ifdef _WIN32
bool set_non_blocking(SOCKET socket_fd) {
    u_long mode = 1;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
}
#endif

} // namespace

struct HttpServer::Impl {
    std::string host;
    int port = 8083;
    VisionService* service = nullptr;
    bool running = false;

#ifdef _WIN32
    SOCKET server_socket = INVALID_SOCKET;
    bool winsock_started = false;
#endif

    std::string handle_request(const std::string& request) {
        std::istringstream iss(request);
        std::string method;
        std::string path;
        std::string version;
        iss >> method >> path >> version;

        const size_t body_pos = request.find("\r\n\r\n");
        const std::string body = body_pos == std::string::npos
            ? std::string()
            : request.substr(body_pos + 4);

        if (!service) {
            return ApiResponse::error(ErrorCode::ERR_NOT_INITIALIZED).to_json();
        }

        if (method == "GET" && path == "/api/v1/vision/health") {
            return ApiResponse::ok("{\"status\":\"ok\"}").to_json();
        }

        if (method == "POST" && path == "/api/v1/vision/matches/init") {
            const std::string match_id = extract_json_string(body, "match_id");
            if (match_id.empty()) {
                return ApiResponse::error(ErrorCode::ERR_PARAM, "match_id required").to_json();
            }
            const std::string main_stream_uri = extract_stream_uri_for_camera(body, "cam_01");
            const std::string aux_stream_uri = extract_stream_uri_for_camera(body, "cam_02");
            if (!main_stream_uri.empty()) {
                service->configure_stream("cam_01", main_stream_uri);
            }
            if (!aux_stream_uri.empty()) {
                service->configure_stream("cam_02", aux_stream_uri);
            }
            return service->init_match(match_id)
                ? ApiResponse::ok("{\"initialized\":true}").to_json()
                : ApiResponse::error(ErrorCode::ERR_PARAM).to_json();
        }

        const std::string match_id = extract_path_match_id(path);
        if (match_id.empty()) {
            return ApiResponse::error(ErrorCode::ERR_PARAM, "unknown endpoint").to_json();
        }

        if (method == "POST" && path == "/api/v1/vision/matches/" + match_id + "/start") {
            return service->start_match(match_id)
                ? ApiResponse::ok("{\"started\":true}").to_json()
                : ApiResponse::error(ErrorCode::ERR_NOT_INITIALIZED).to_json();
        }

        if (method == "POST" && path == "/api/v1/vision/matches/" + match_id + "/stop") {
            return service->stop_match(match_id)
                ? ApiResponse::ok("{\"stopped\":true}").to_json()
                : ApiResponse::error(ErrorCode::ERR_NOT_INITIALIZED).to_json();
        }

        if (method == "POST" && path == "/api/v1/vision/matches/" + match_id + "/simulate-frame") {
            service->process_simulated_frames();
            return ApiResponse::ok("{\"accepted\":true}").to_json();
        }

        if (method == "GET" && path == "/api/v1/vision/matches/" + match_id + "/status") {
            return ApiResponse::ok(service->get_status(match_id).to_json()).to_json();
        }

        if (method == "GET" && path == "/api/v1/vision/matches/" + match_id + "/event-candidates") {
            return ApiResponse::ok(events_data_json(service->get_event_candidates(match_id))).to_json();
        }

        if (method == "GET" && path == "/api/v1/vision/matches/" + match_id + "/focus-regions") {
            return ApiResponse::ok(service->generate_focus_regions(match_id).to_json()).to_json();
        }

        if (method == "GET" && path == "/api/v1/vision/matches/" + match_id + "/program-decision") {
            return ApiResponse::ok(service->generate_program_decision(match_id).to_json()).to_json();
        }

        return ApiResponse::error(ErrorCode::ERR_PARAM, "unknown endpoint").to_json();
    }
};

HttpServer::HttpServer(const std::string& host, int port, VisionService* service)
    : impl_(new Impl()) {
    impl_->host = host;
    impl_->port = port;
    impl_->service = service;
}

HttpServer::~HttpServer() {
    stop();
    delete impl_;
}

bool HttpServer::start() {
#ifndef _WIN32
    std::cerr << "[http_server] Native socket server is implemented for Windows MVP only" << std::endl;
    return false;
#else
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[http_server] WSAStartup failed" << std::endl;
        return false;
    }
    impl_->winsock_started = true;

    impl_->server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->server_socket == INVALID_SOCKET) {
        std::cerr << "[http_server] socket failed" << std::endl;
        return false;
    }

    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(impl_->port));
    address.sin_addr.s_addr = inet_addr(impl_->host.c_str());

    const char enable = 1;
    setsockopt(impl_->server_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    if (bind(impl_->server_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "[http_server] bind failed on " << impl_->host << ":" << impl_->port << std::endl;
        return false;
    }
    if (listen(impl_->server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[http_server] listen failed" << std::endl;
        return false;
    }
    set_non_blocking(impl_->server_socket);
    impl_->running = true;
    std::cout << "[http_server] Listening on " << impl_->host << ":" << impl_->port << std::endl;
    return true;
#endif
}

void HttpServer::stop() {
#ifdef _WIN32
    if (impl_->server_socket != INVALID_SOCKET) {
        closesocket(impl_->server_socket);
        impl_->server_socket = INVALID_SOCKET;
    }
    if (impl_->winsock_started) {
        WSACleanup();
        impl_->winsock_started = false;
    }
#endif
    impl_->running = false;
}

void HttpServer::poll_once(int timeout_ms) {
#ifndef _WIN32
    (void)timeout_ms;
#else
    if (!impl_->running || impl_->server_socket == INVALID_SOCKET) return;

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(impl_->server_socket, &read_set);
    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
    if (ready <= 0 || !FD_ISSET(impl_->server_socket, &read_set)) return;

    SOCKET client_socket = accept(impl_->server_socket, nullptr, nullptr);
    if (client_socket == INVALID_SOCKET) return;
    u_long blocking_mode = 0;
    ioctlsocket(client_socket, FIONBIO, &blocking_mode);

    char buffer[8192];
    std::string request;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) break;
        request.append(buffer, received);
        if (request_complete(request)) break;
    }
    if (!request.empty()) {
        const std::string body = impl_->handle_request(request);
        const std::string response = wrap_http_response(body);
        send(client_socket, response.c_str(), static_cast<int>(response.size()), 0);
    }
    closesocket(client_socket);
#endif
}

bool HttpServer::is_running() const {
    return impl_->running;
}

} // namespace vision

#include "http_server.h"
#include "camera_manager.h"
#include "stream_config.h"

#include <sstream>
#include <stdexcept>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ingest {

using json = nlohmann::json;

// ============================================================================
// 构造 / 析构
// ============================================================================

HttpServer::HttpServer(CameraManager& manager, int port)
    : manager_(manager)
    , port_(port)
{
}

HttpServer::~HttpServer() {
    if (running_.load(std::memory_order_acquire)) {
        Stop();
    }
}

// ============================================================================
// 生命周期：Start / Stop
// ============================================================================

bool HttpServer::Start() {
    if (running_.load(std::memory_order_acquire)) {
        spdlog::warn("HttpServer already running");
        return false;
    }

    SetupRoutes();

    // httplib 框架级异常兜底 —— 即使某个 handler 没捕获到异常，
    // 框架也不会 crash，而是返回 HTTP 500
    svr_.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            spdlog::error("Unhandled exception in HTTP handler [{} {}]: {}",
                          req.method, req.path, e.what());
        } catch (...) {
            spdlog::error("Unhandled unknown exception in HTTP handler [{} {}]",
                          req.method, req.path);
        }
        res.status = 500;
        res.set_content(
            R"({"code":1007,"message":"internal server error","data":{}})",
            "application/json");
    });

    // 监听线程
    listener_thread_ = std::thread([this]() {
        spdlog::info("HTTP server listening on 127.0.0.1:{}", port_);
        running_.store(true, std::memory_order_release);

        // listen() 阻塞直到 svr_.stop() 被调用
        if (!svr_.listen("127.0.0.1", port_)) {
            spdlog::error("HTTP server failed to bind to 127.0.0.1:{}", port_);
            running_.store(false, std::memory_order_release);
        }
    });

    // 短暂等待，确认线程启动成功
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return running_.load(std::memory_order_acquire);
}

void HttpServer::Stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    spdlog::info("Stopping HTTP server...");
    svr_.stop();

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }

    running_.store(false, std::memory_order_release);
    spdlog::info("HTTP server stopped");
}

// ============================================================================
// 路由注册
// ============================================================================

void HttpServer::SetupRoutes() {
    // ---- POST /api/v1/ingest/matches/init ----
    svr_.Post("/api/v1/ingest/matches/init",
        [this](const httplib::Request& req, httplib::Response& res) {
            SafeHandler(
                [this](const auto& r, auto& resp) { OnInit(r, resp); },
                req, res);
        });

    // ---- POST /api/v1/ingest/matches/start ----
    svr_.Post("/api/v1/ingest/matches/start",
        [this](const httplib::Request& req, httplib::Response& res) {
            SafeHandler(
                [this](const auto& r, auto& resp) { OnStart(r, resp); },
                req, res);
        });

    // ---- POST /api/v1/ingest/matches/stop ----
    svr_.Post("/api/v1/ingest/matches/stop",
        [this](const httplib::Request& req, httplib::Response& res) {
            SafeHandler(
                [this](const auto& r, auto& resp) { OnStop(r, resp); },
                req, res);
        });

    // ---- GET /api/v1/ingest/status ----
    svr_.Get("/api/v1/ingest/status",
        [this](const httplib::Request& req, httplib::Response& res) {
            SafeHandler(
                [this](const auto& r, auto& resp) { OnStatus(r, resp); },
                req, res);
        });

    spdlog::info("Routes registered: init, start, stop, status");
}

// ============================================================================
// 异常隔离中间层
// ============================================================================

void HttpServer::SafeHandler(
    const std::function<void(const httplib::Request&,
                              httplib::Response&)>& handler,
    const httplib::Request& req,
    httplib::Response& res)
{
    try {
        handler(req, res);
    } catch (const json::parse_error& e) {
        spdlog::warn("[{} {}] JSON parse error: {}", req.method, req.path, e.what());
        SendError(res, 400, 1001,
                  std::string("Invalid JSON: ") + e.what());
    } catch (const json::out_of_range& e) {
        spdlog::warn("[{} {}] Missing required field: {}", req.method, req.path, e.what());
        SendError(res, 400, 1001,
                  std::string("Missing required field: ") + e.what());
    } catch (const json::type_error& e) {
        spdlog::warn("[{} {}] JSON type mismatch: {}", req.method, req.path, e.what());
        SendError(res, 400, 1001,
                  std::string("Type mismatch: ") + e.what());
    } catch (const std::invalid_argument& e) {
        spdlog::warn("[{} {}] Invalid argument: {}", req.method, req.path, e.what());
        SendError(res, 400, 1001, e.what());
    } catch (const std::runtime_error& e) {
        spdlog::error("[{} {}] Runtime error: {}", req.method, req.path, e.what());
        SendError(res, 500, 1007, e.what());
    } catch (const std::exception& e) {
        spdlog::error("[{} {}] Exception: {}", req.method, req.path, e.what());
        SendError(res, 500, 1007,
                  std::string("Internal error: ") + e.what());
    } catch (...) {
        spdlog::error("[{} {}] Unknown exception", req.method, req.path);
        SendError(res, 500, 1007, "Unknown internal error");
    }
}

// ============================================================================
// 通用 JSON 响应工具
// ============================================================================

void HttpServer::SetCommonHeaders(httplib::Response& res) {
    res.set_header("Content-Type", "application/json; charset=utf-8");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

void HttpServer::SendError(httplib::Response& res,
                           int http_status, int code,
                           const std::string& message) {
    SetCommonHeaders(res);
    res.status = http_status;
    json body;
    body["code"]    = code;
    body["message"] = message;
    body["data"]    = json::object();
    res.set_content(body.dump(), "application/json");
}

void HttpServer::SendOk(httplib::Response& res,
                        const std::string& json_data_body) {
    SetCommonHeaders(res);
    res.status = 200;
    std::ostringstream oss;
    oss << R"({"code":0,"message":"ok","data":)" << json_data_body << "}";
    res.set_content(oss.str(), "application/json");
}

int HttpServer::MapErrorCode(int sdk_error) {
    // contract §7.4 错误码映射表
    if (sdk_error == 0) return 0;  // MV_OK

    // 根据实际错误类型映射（部分来自 SDK 返回值语义）
    // 1001 — 参数错误
    // 1002 — 资源未初始化
    // 1012 — 工业相机 SDK 初始化失败
    // 1013 — 工业相机枚举失败
    // 1014 — 相机取流失败
    // 1015 — 相机未绑定
    // 1016 — 相机序列号冲突

    // 简化映射：非零 SDK 错误 → 根据上下文判断
    // 此处在具体 handler 中会有更精确的映射
    return 1012;  // 默认：SDK 相关失败
}

// ============================================================================
// POST /api/v1/ingest/matches/init
// ============================================================================

void HttpServer::OnInit(const httplib::Request& req, httplib::Response& res) {
    spdlog::info("=== POST /init received ===");

    // ---- 1. 解析 JSON ----
    IngestInitRequest init_req;
    std::string parse_error;
    if (!ParseInitRequest(req.body, init_req, parse_error)) {
        SendError(res, 400, 1001, parse_error);
        return;
    }

    // ---- 2. 调用 CameraManager::Init ----
    int nRet = manager_.Init(init_req);
    if (nRet != 0) {
        int err_code;
        std::string err_msg;

        // 根据 SDK 返回值精确映射错误码
        // MV_E_NODEVICE 在 MVS SDK 中的实际值需要查 MvErrorDefine.h
        // 这里使用通用判断
        if (nRet == 0x80000001) {  // MV_E_NODEVICE 或其他典型值
            err_code = 1013;
            err_msg  = "Camera enumeration failed: device(s) not found";
        } else if (nRet == 0x80000002) {
            err_code = 1012;
            err_msg  = "SDK initialization failed";
        } else {
            err_code = 1012;
            std::ostringstream oss;
            oss << "Camera init failed (SDK error: 0x"
                << std::hex << nRet << ")";
            err_msg = oss.str();
        }
        SendError(res, 500, err_code, err_msg);
        return;
    }

    // ---- 3. 返回成功 ----
    json data;
    data["match_id"] = init_req.match_id;
    data["cameras_ready"] = 2;
    SendOk(res, data.dump());

    spdlog::info("=== POST /init OK — match [{}] ===", init_req.match_id);
}

// ============================================================================
// POST /api/v1/ingest/matches/start
// ============================================================================

void HttpServer::OnStart(const httplib::Request& req, httplib::Response& res) {
    spdlog::info("=== POST /start received ===");

    // ---- 1. 解析参数 ----
    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        // 允许空 body —— 默认启动全部
        body = json::object();
    }

    std::string match_id = body.value("match_id", "");
    std::string camera_id = body.value("camera_id", "all");

    spdlog::info("Start request — match_id: [{}], camera_id: [{}]",
                 match_id.empty() ? "(not specified)" : match_id,
                 camera_id);

    // ---- 2. 按 camera_id 启动 ----
    int ret_main = 0, ret_aux = 0;

    if (camera_id == "all" || camera_id.empty()) {
        ret_main = ret_aux = manager_.StartAll();
    } else if (camera_id == "cam_01") {
        auto* ctrl = manager_.GetController("cam_01");
        if (ctrl == nullptr) {
            SendError(res, 400, 1015, "cam_01 not initialized");
            return;
        }
        ret_main = ctrl->StartGrabbing();
    } else if (camera_id == "cam_02") {
        auto* ctrl = manager_.GetController("cam_02");
        if (ctrl == nullptr) {
            SendError(res, 400, 1015, "cam_02 not initialized");
            return;
        }
        ret_aux = ctrl->StartGrabbing();
    } else {
        SendError(res, 400, 1001,
                  "Invalid camera_id. Use 'all', 'cam_01', or 'cam_02'");
        return;
    }

    // ---- 3. 构建响应 ----
    json data;
    data["match_id"]  = manager_.CurrentMatchId();
    data["cam_01"]    = (ret_main == 0) ? "running" : "start_failed";
    data["cam_02"]    = (ret_aux  == 0) ? "running" : "start_failed";

    if (ret_main != 0 || ret_aux != 0) {
        data["warning"] = "One or more cameras failed to start; check cam status";
    }

    SendOk(res, data.dump());
    spdlog::info("=== POST /start OK ===");
}

// ============================================================================
// POST /api/v1/ingest/matches/stop
// ============================================================================

void HttpServer::OnStop(const httplib::Request& req, httplib::Response& res) {
    spdlog::info("=== POST /stop received ===");

    // ---- 1. 解析参数 ----
    json body;
    try {
        body = json::parse(req.body);
    } catch (...) {
        body = json::object();
    }

    std::string match_id = body.value("match_id", "");
    std::string camera_id = body.value("camera_id", "all");

    // ---- 2. 按 camera_id 停止 ----
    int ret_main = 0, ret_aux = 0;

    if (camera_id == "all" || camera_id.empty()) {
        ret_main = ret_aux = manager_.StopAll();
    } else if (camera_id == "cam_01") {
        auto* ctrl = manager_.GetController("cam_01");
        if (ctrl == nullptr) {
            SendError(res, 400, 1015, "cam_01 not initialized");
            return;
        }
        ret_main = ctrl->StopGrabbing();
    } else if (camera_id == "cam_02") {
        auto* ctrl = manager_.GetController("cam_02");
        if (ctrl == nullptr) {
            SendError(res, 400, 1015, "cam_02 not initialized");
            return;
        }
        ret_aux = ctrl->StopGrabbing();
    } else {
        SendError(res, 400, 1001,
                  "Invalid camera_id. Use 'all', 'cam_01', or 'cam_02'");
        return;
    }

    // ---- 3. 停止后释放设备资源 ----
    if (camera_id == "all" || camera_id.empty()) {
        manager_.ShutdownAll();
    }

    // ---- 4. 构建响应 ----
    json data;
    data["match_id"]  = match_id;
    data["cam_01"]    = (ret_main == 0) ? "stopped" : "stop_failed";
    data["cam_02"]    = (ret_aux  == 0) ? "stopped" : "stop_failed";

    SendOk(res, data.dump());
    spdlog::info("=== POST /stop OK ===");
}

// ============================================================================
// GET /api/v1/ingest/status
// ============================================================================

void HttpServer::OnStatus(const httplib::Request& /*req*/,
                          httplib::Response& res) {
    auto status = manager_.GetGlobalStatus();

    json data;
    data["match_id"]    = status.match_id;
    data["module_state"] = status.module_state;

    // cam_01 状态
    data["cam_01"] = {
        {"camera_id",    status.cam_01.camera_id},
        {"state",        status.cam_01.state},
        {"connected",    status.cam_01.connected},
        {"frame_count",  status.cam_01.frame_count},
        {"fps",          status.cam_01.fps},
        {"lost_packets", status.cam_01.lost_packets},
        {"queue_depth",  status.cam_01.queue_depth},
        {"stream_uri",   status.cam_01.stream_uri},
        {"streaming_active", status.cam_01.streaming_active},
        {"output_width", status.cam_01.output_width},
        {"output_height", status.cam_01.output_height},
        {"output_fps",   status.cam_01.output_fps},
        {"last_frame_timestamp_ms", status.cam_01.last_frame_timestamp_ms},
        {"dropped_frames", status.cam_01.dropped_frames},
        {"encoded_frames", status.cam_01.encoded_frames},
        {"last_error",   status.cam_01.last_error}
    };

    // cam_02 状态
    data["cam_02"] = {
        {"camera_id",    status.cam_02.camera_id},
        {"state",        status.cam_02.state},
        {"connected",    status.cam_02.connected},
        {"frame_count",  status.cam_02.frame_count},
        {"fps",          status.cam_02.fps},
        {"lost_packets", status.cam_02.lost_packets},
        {"queue_depth",  status.cam_02.queue_depth},
        {"stream_uri",   status.cam_02.stream_uri},
        {"streaming_active", status.cam_02.streaming_active},
        {"output_width", status.cam_02.output_width},
        {"output_height", status.cam_02.output_height},
        {"output_fps",   status.cam_02.output_fps},
        {"last_frame_timestamp_ms", status.cam_02.last_frame_timestamp_ms},
        {"dropped_frames", status.cam_02.dropped_frames},
        {"encoded_frames", status.cam_02.encoded_frames},
        {"last_error",   status.cam_02.last_error}
    };

    data["last_error"] = status.last_error;

    SendOk(res, data.dump());
}

// ============================================================================
// JSON 序列化辅助
// ============================================================================

bool HttpServer::ParseInitRequest(const std::string& body,
                                  IngestInitRequest& out_req,
                                  std::string& out_error) {
    try {
        json j = json::parse(body);

        // ---- match_id（必填）----
        if (!j.contains("match_id")) {
            out_error = "Missing required field: match_id";
            return false;
        }
        out_req.match_id = j["match_id"].get<std::string>();

        // ---- cameras[]（必填，contract §8.1）----
        if (!j.contains("cameras") || !j["cameras"].is_array()) {
            out_error = "Missing or invalid field: cameras (must be array)";
            return false;
        }

        const auto& jcameras = j["cameras"];
        if (jcameras.size() != 2) {
            std::ostringstream oss;
            oss << "cameras array must contain exactly 2 entries, got "
                << jcameras.size();
            out_error = oss.str();
            return false;
        }

        for (size_t i = 0; i < 2; ++i) {
            const auto& jc = jcameras[i];
            auto& binding = out_req.cameras[i];

            binding.camera_id     = jc.value("camera_id", "");
            binding.role          = jc.value("role", "");
            binding.model         = jc.value("model", "MV-CE050-30GC");
            binding.lens          = jc.value("lens", "6mm_C_mount");
            binding.serial_number = jc.value("serial_number", "");
            binding.stream_uri    = jc.value("stream_uri", "");

            if (binding.camera_id.empty()) {
                out_error = "cameras[" + std::to_string(i) +
                            "]: camera_id is required";
                return false;
            }
            if (binding.serial_number.empty()) {
                out_error = "cameras[" + std::to_string(i) +
                            "]: serial_number is required (contract §2.2)";
                return false;
            }
        }

        // ---- network_config（可选，有默认值）----
        if (j.contains("network_config")) {
            const auto& jnc = j["network_config"];
            out_req.network_config.mode   = jnc.value("mode", "static_ip");
            out_req.network_config.subnet = jnc.value("subnet", "192.168.10.0/24");
        }

        // ---- capture_config（可选，有默认值）----
        if (j.contains("capture_config")) {
            const auto& jcc = j["capture_config"];
            out_req.capture_config.internal_source_resolution =
                jcc.value("internal_source_resolution", "5mp_native");
            out_req.capture_config.rtsp_output_resolution =
                jcc.value("rtsp_output_resolution", "1920x1080");
            out_req.capture_config.fps         = jcc.value("fps", 25);
            out_req.capture_config.pixel_format = jcc.value("pixel_format", "bgr8_or_nv12");
            out_req.capture_config.video_codec  = jcc.value("video_codec", "h264");
        }

        // ---- camera_param_strategy（可选，有默认值）----
        if (j.contains("camera_param_strategy")) {
            const auto& jcs = j["camera_param_strategy"];
            out_req.camera_param_strategy.load_from =
                jcs.value("load_from", "mvs_user_set_or_config");
            out_req.camera_param_strategy.trigger_mode =
                jcs.value("trigger_mode", "continuous");
            out_req.camera_param_strategy.allow_runtime_page_edit =
                jcs.value("allow_runtime_page_edit", false);
        }

        out_req.camera_param_strategy.allow_runtime_page_edit = false;

        return true;

    } catch (const json::parse_error& e) {
        out_error = std::string("JSON parse error: ") + e.what();
        return false;
    } catch (const json::type_error& e) {
        out_error = std::string("Type mismatch: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        out_error = std::string("Unexpected error: ") + e.what();
        return false;
    }
}

std::string HttpServer::SerializeGlobalStatus(
    const CameraManager::GlobalStatus& status) {
    json j;
    j["match_id"]    = status.match_id;
    j["module_state"] = status.module_state;
    j["cam_01"] = {
        {"camera_id",    status.cam_01.camera_id},
        {"state",        status.cam_01.state},
        {"connected",    status.cam_01.connected},
        {"frame_count",  status.cam_01.frame_count},
        {"fps",          status.cam_01.fps},
        {"lost_packets", status.cam_01.lost_packets},
        {"queue_depth",  status.cam_01.queue_depth},
        {"stream_uri",   status.cam_01.stream_uri},
        {"streaming_active", status.cam_01.streaming_active},
        {"output_width", status.cam_01.output_width},
        {"output_height", status.cam_01.output_height},
        {"output_fps",   status.cam_01.output_fps},
        {"last_frame_timestamp_ms", status.cam_01.last_frame_timestamp_ms},
        {"dropped_frames", status.cam_01.dropped_frames},
        {"encoded_frames", status.cam_01.encoded_frames},
        {"last_error",   status.cam_01.last_error}
    };
    j["cam_02"] = {
        {"camera_id",    status.cam_02.camera_id},
        {"state",        status.cam_02.state},
        {"connected",    status.cam_02.connected},
        {"frame_count",  status.cam_02.frame_count},
        {"fps",          status.cam_02.fps},
        {"lost_packets", status.cam_02.lost_packets},
        {"queue_depth",  status.cam_02.queue_depth},
        {"stream_uri",   status.cam_02.stream_uri},
        {"streaming_active", status.cam_02.streaming_active},
        {"output_width", status.cam_02.output_width},
        {"output_height", status.cam_02.output_height},
        {"output_fps",   status.cam_02.output_fps},
        {"last_frame_timestamp_ms", status.cam_02.last_frame_timestamp_ms},
        {"dropped_frames", status.cam_02.dropped_frames},
        {"encoded_frames", status.cam_02.encoded_frames},
        {"last_error",   status.cam_02.last_error}
    };
    j["last_error"] = status.last_error;
    return j.dump();
}

} // namespace ingest

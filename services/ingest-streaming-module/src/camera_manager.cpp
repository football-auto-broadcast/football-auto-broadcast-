#include "camera_manager.h"

#include <cstring>
#include <sstream>
#include <vector>

// SDK 未提供"设备未找到"错误码；用 MV_E_NODATA 表示枚举结果中无匹配设备
#ifndef MV_E_NODEVICE
#define MV_E_NODEVICE  MV_E_NODATA
#endif

#include "spdlog/spdlog.h"

namespace ingest {

// ============================================================================
// 构造 / 析构
// ============================================================================

CameraManager::CameraManager()
    : sdk_initialized_(false)
{
    // 控制器延迟创建——在 Init() 中根据枚举匹配结果创建
}

CameraManager::~CameraManager() {
    if (cam_01_ || cam_02_) {
        ShutdownAll();
    }
    if (sdk_initialized_) {
        FinalizeSDK();
    }
}

// ============================================================================
// SDK 全局生命周期
// ============================================================================

int CameraManager::InitializeSDK() {
    if (sdk_initialized_) {
        spdlog::warn("SDK already initialized");
        return MV_OK;
    }

    int nRet = MV_CC_Initialize();
    if (MV_OK != nRet) {
        std::ostringstream err;
        err << "MV_CC_Initialize failed: 0x" << std::hex << nRet;
        last_error_ = err.str();
        spdlog::critical("{}", last_error_);
        return nRet;
    }

    sdk_initialized_ = true;
    spdlog::info("MVS SDK initialized (version: {}.{}.{}.{})",
                 (MV_CC_GetSDKVersion() >> 24) & 0xFF,
                 (MV_CC_GetSDKVersion() >> 16) & 0xFF,
                 (MV_CC_GetSDKVersion() >> 8)  & 0xFF,
                 MV_CC_GetSDKVersion() & 0xFF);
    return MV_OK;
}

int CameraManager::FinalizeSDK() {
    if (!sdk_initialized_) return MV_OK;

    int nRet = MV_CC_Finalize();
    if (MV_OK != nRet) {
        spdlog::warn("MV_CC_Finalize returned: 0x{:x}", nRet);
    }
    sdk_initialized_ = false;
    spdlog::info("MVS SDK finalized");
    return nRet;
}

// ============================================================================
// E 模块控制接口
// ============================================================================

int CameraManager::Init(const IngestInitRequest& req) {
    if (!sdk_initialized_) {
        last_error_ = "Init: SDK not initialized";
        return MV_E_RESOURCE;
    }

    match_id_ = req.match_id;
    spdlog::info("=== Initializing match [{}] ===", match_id_);

    // ---- 从请求中提取双路序列号（contract §8.1 cameras[] 字段）----
    std::string serial_main;
    std::string serial_aux;
    for (const auto& cam : req.cameras) {
        if (cam.camera_id == "cam_01") {
            serial_main = cam.serial_number;
        } else if (cam.camera_id == "cam_02") {
            serial_aux = cam.serial_number;
        }
    }

    if (serial_main.empty() || serial_aux.empty()) {
        last_error_ = "Init: cameras[] missing serial_number for cam_01 or cam_02";
        spdlog::error("{}", last_error_);
        return MV_E_PARAMETER;
    }

    spdlog::info("Target cameras — main: [{}], aux: [{}]", serial_main, serial_aux);

    // ---- 枚举 + 序列号匹配 ----
    MV_CC_DEVICE_INFO* pMainInfo = nullptr;
    MV_CC_DEVICE_INFO* pAuxInfo  = nullptr;

    int nRet = EnumerateAndMatch(serial_main, serial_aux, pMainInfo, pAuxInfo);
    if (MV_OK != nRet) {
        last_error_ = "EnumerateAndMatch failed: " +
                      (nRet == MV_E_NODEVICE ? "device(s) not found"
                       : "SDK error 0x" + std::to_string(nRet));
        spdlog::error("{}", last_error_);
        return nRet;
    }

    // ---- 创建双相机控制器 ----
    cam_01_ = std::make_unique<CameraController>("cam_01", serial_main);
    cam_02_ = std::make_unique<CameraController>("cam_02", serial_aux);

    // ---- 初始化 cam_01 ----
    nRet = cam_01_->Initialize(pMainInfo);
    if (MV_OK != nRet) {
        last_error_ = "cam_01 Initialize failed";
        return nRet;
    }
    nRet = cam_01_->Open();
    if (MV_OK != nRet) {
        last_error_ = "cam_01 Open failed";
        return nRet;
    }

    // ---- 初始化 cam_02 ----
    nRet = cam_02_->Initialize(pAuxInfo);
    if (MV_OK != nRet) {
        last_error_ = "cam_02 Initialize failed";
        return nRet;
    }
    nRet = cam_02_->Open();
    if (MV_OK != nRet) {
        last_error_ = "cam_02 Open failed";
        return nRet;
    }

    // ---- 从请求中提取 RTSP URI 和输出参数（contract §8.1 cameras[].stream_uri）----
    for (const auto& cam : req.cameras) {
        auto* ctrl = GetController(cam.camera_id);
        if (ctrl == nullptr) continue;

        if (!cam.stream_uri.empty()) {
            ctrl->SetRtspUri(cam.stream_uri);
        }

        // 解析输出分辨率（格式："1920x1080" → 宽x高）
        std::string res_str = req.capture_config.rtsp_output_resolution;
        int w = 1920, h = 1080;
        auto x_pos = res_str.find('x');
        if (x_pos != std::string::npos) {
            w = std::stoi(res_str.substr(0, x_pos));
            h = std::stoi(res_str.substr(x_pos + 1));
        }
        ctrl->SetOutputParams(w, h, req.capture_config.fps);
    }

    spdlog::info("=== Match [{}] initialized: both cameras ready ===", match_id_);
    return MV_OK;
}

int CameraManager::StartAll() {
    if (!cam_01_ || !cam_02_) {
        last_error_ = "StartAll: cameras not initialized";
        return MV_E_RESOURCE;
    }

    spdlog::info("Starting dual-camera grabbing for match [{}]...", match_id_);

    // 顺序启动（先主机位，后辅机位）
    int nRet = cam_01_->StartGrabbing();
    if (MV_OK != nRet) {
        last_error_ = "cam_01 StartGrabbing failed";
        return nRet;
    }

    nRet = cam_02_->StartGrabbing();
    if (MV_OK != nRet) {
        last_error_ = "cam_02 StartGrabbing failed";
        // cam_01 继续保持取流（降级策略 contract §9.5）
        spdlog::warn("cam_02 StartGrabbing failed, continuing with cam_01 only");
    }

    spdlog::info("Dual-camera grabbing started for match [{}]", match_id_);
    return MV_OK;
}

int CameraManager::StopAll() {
    spdlog::info("Stopping dual-camera grabbing for match [{}]...", match_id_);

    int ret_main = MV_OK, ret_aux = MV_OK;

    if (cam_01_) {
        ret_main = cam_01_->StopGrabbing();
    }
    if (cam_02_) {
        ret_aux = cam_02_->StopGrabbing();
    }

    if (MV_OK != ret_main) {
        last_error_ = "cam_01 StopGrabbing returned non-zero";
    }
    if (MV_OK != ret_aux) {
        last_error_ += (last_error_.empty() ? "" : "; ") +
                       std::string("cam_02 StopGrabbing returned non-zero");
    }

    spdlog::info("Dual-camera grabbing stopped for match [{}]", match_id_);
    return (ret_main == MV_OK && ret_aux == MV_OK) ? MV_OK : MV_E_CALLORDER;
}

int CameraManager::ShutdownAll() {
    spdlog::info("Shutting down cameras for match [{}]...", match_id_);

    if (cam_01_) {
        cam_01_->Close();
        cam_01_.reset();
    }
    if (cam_02_) {
        cam_02_->Close();
        cam_02_.reset();
    }

    match_id_.clear();
    spdlog::info("All cameras shut down");
    return MV_OK;
}

// ============================================================================
// 帧消费入口
// ============================================================================

CameraController* CameraManager::GetController(const std::string& camera_id) const {
    if (camera_id == "cam_01") return cam_01_.get();
    if (camera_id == "cam_02") return cam_02_.get();
    return nullptr;
}

bool CameraManager::TryPopMainFrame(FrameData& out_frame) {
    if (!cam_01_) return false;
    return cam_01_->TryPopFrame(out_frame);
}

bool CameraManager::TryPopAuxFrame(FrameData& out_frame) {
    if (!cam_02_) return false;
    return cam_02_->TryPopFrame(out_frame);
}

// ============================================================================
// 全局状态汇总
// ============================================================================

CameraManager::GlobalStatus CameraManager::GetGlobalStatus() const {
    GlobalStatus gs;
    gs.match_id = match_id_;

    // 汇总模块级状态
    if (!sdk_initialized_) {
        gs.module_state = "idle";
    } else if (!cam_01_ && !cam_02_) {
        gs.module_state = "idle";
    } else {
        auto s1 = cam_01_ ? cam_01_->State() : CameraState::Idle;
        auto s2 = cam_02_ ? cam_02_->State() : CameraState::Idle;

        if (s1 == CameraState::Running && s2 == CameraState::Running) {
            gs.module_state = "running";
        } else if (s1 == CameraState::Degraded || s2 == CameraState::Degraded) {
            gs.module_state = "degraded";
        } else if (s1 == CameraState::Failed || s2 == CameraState::Failed) {
            gs.module_state = "degraded";  // 单路失败仍算降级
        } else if (s1 == CameraState::Stopped && s2 == CameraState::Stopped) {
            gs.module_state = "stopped";
        } else {
            gs.module_state = "initializing";
        }
    }

    gs.cam_01     = cam_01_ ? cam_01_->GetStatusSummary()
                            : CameraController::StatusSummary{"cam_01", "idle"};
    gs.cam_02     = cam_02_ ? cam_02_->GetStatusSummary()
                            : CameraController::StatusSummary{"cam_02", "idle"};
    gs.last_error = last_error_;
    return gs;
}

// ============================================================================
// 设备枚举与序列号匹配
// ============================================================================

int CameraManager::EnumerateAndMatch(
    const std::string& serial_main,
    const std::string& serial_aux,
    MV_CC_DEVICE_INFO*& out_main,
    MV_CC_DEVICE_INFO*& out_aux)
{
    // ---- 枚举 GigE 设备 ----
    MV_CC_DEVICE_INFO_LIST stDevList;
    std::memset(&stDevList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    // contract 确认：使用标准 GigE 协议枚举
    unsigned int nLayerType = MV_GIGE_DEVICE;

    int nRet = MV_CC_EnumDevices(nLayerType, &stDevList);
    if (MV_OK != nRet) {
        spdlog::error("MV_CC_EnumDevices failed: 0x{:x}", nRet);
        return nRet;
    }

    if (stDevList.nDeviceNum == 0) {
        spdlog::error("No GigE devices found on the network");
        return MV_E_NODEVICE;
    }

    spdlog::info("Found {} GigE device(s)", stDevList.nDeviceNum);

    // ---- 打印所有枚举到的设备（用于调试排查）----
    for (unsigned int i = 0; i < stDevList.nDeviceNum; ++i) {
        auto* pDev = stDevList.pDeviceInfo[i];
        if (pDev == nullptr) continue;

        std::string serial(
            reinterpret_cast<const char*>(
                pDev->SpecialInfo.stGigEInfo.chSerialNumber));

        // 打印 IP
        uint32_t ip = pDev->SpecialInfo.stGigEInfo.nCurrentIp;
        spdlog::info("  [{}] Serial: {}, IP: {}.{}.{}.{}, Model: {}",
                     i,
                     serial,
                     (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                     (ip >> 8) & 0xFF, ip & 0xFF,
                     reinterpret_cast<const char*>(
                         pDev->SpecialInfo.stGigEInfo.chModelName));
    }

    // ---- 按序列号匹配（contract §2.2：禁止按枚举顺序隐式绑定）----
    out_main = nullptr;
    out_aux  = nullptr;

    for (unsigned int i = 0; i < stDevList.nDeviceNum; ++i) {
        auto* pDev = stDevList.pDeviceInfo[i];
        if (pDev == nullptr) continue;

        std::string serial(
            reinterpret_cast<const char*>(
                pDev->SpecialInfo.stGigEInfo.chSerialNumber));

        if (serial == serial_main) {
            out_main = pDev;
            spdlog::info("Matched main camera (cam_01): serial [{}] at device index {}",
                         serial, i);
        } else if (serial == serial_aux) {
            out_aux = pDev;
            spdlog::info("Matched aux camera (cam_02): serial [{}] at device index {}",
                         serial, i);
        }
    }

    // ---- 检查匹配结果 ----
    if (out_main == nullptr) {
        spdlog::error("Main camera serial [{}] not found in enumerated devices",
                      serial_main);
        return MV_E_NODEVICE;
    }
    if (out_aux == nullptr) {
        spdlog::error("Aux camera serial [{}] not found in enumerated devices",
                      serial_aux);
        return MV_E_NODEVICE;
    }

    return MV_OK;
}

} // namespace ingest

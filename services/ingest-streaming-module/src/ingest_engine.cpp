#include "ingest_engine.h"
#include <iostream>
#include <chrono>
#include <cstdio>

IngestEngine::IngestEngine() {}

IngestEngine::~IngestEngine() {
    Stop();
}

bool IngestEngine::Initialize(const IngestConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_status != Status::idle) {
        std::cout << "[ERROR] IngestEngine is not in idle state" << std::endl;
        return false;
    }

    m_config = config;
    m_status = Status::initializing;
    m_lastReconnectAttempts.assign(config.cameras.size(), std::chrono::steady_clock::time_point{});

    // --- Enumerate devices ONCE globally (was per-camera with double call) ---
    // Calling MV_CC_EnumDevices per-camera or twice in a row triggers
    // MV_E_CALLORDER (0x80000003) in MV_CC_OpenDevice.
    MV_CC_DEVICE_INFO_LIST devList = { 0 };
    int enumRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);
    if (enumRet != MV_OK) {
        std::cout << "[ERROR] Device enumeration failed. Error: 0x"
                  << std::hex << enumRet << std::dec << std::endl;
        m_status = Status::failed;
        return false;
    }
    std::cout << "[INFO] Enumerated " << devList.nDeviceNum << " devices" << std::endl;

    for (const auto& camConfig : config.cameras) {
        auto camera = std::make_unique<CameraDevice>(
            camConfig.serial, camConfig.camera_id, camConfig.role,
            camConfig.width, camConfig.height);

        // Find matching device info by serial and log network config
        const MV_CC_DEVICE_INFO* matchedInfo = nullptr;
        for (unsigned int i = 0; i < devList.nDeviceNum; i++) {
            if (devList.pDeviceInfo[i]->nTLayerType != MV_GIGE_DEVICE) continue;

            const auto& gige = devList.pDeviceInfo[i]->SpecialInfo.stGigEInfo;
            std::string devSerial = (char*)gige.chSerialNumber;

            // Log camera network info for diagnostics
            unsigned int camIp = gige.nCurrentIp;
            unsigned int camMask = gige.nCurrentSubNetMask;
            unsigned int camGw = gige.nDefultGateWay;
            unsigned int hostAdapterIp = gige.nNetExport;

            std::cout << "[DEBUG] Found GigE camera: serial=" << devSerial
                      << " IP=" << ((camIp >> 24) & 0xFF) << "." << ((camIp >> 16) & 0xFF)
                      << "." << ((camIp >> 8) & 0xFF) << "." << (camIp & 0xFF)
                      << " Mask=" << ((camMask >> 24) & 0xFF) << "." << ((camMask >> 16) & 0xFF)
                      << "." << ((camMask >> 8) & 0xFF) << "." << (camMask & 0xFF)
                      << " HostAdapter=" << ((hostAdapterIp >> 24) & 0xFF)
                      << "." << ((hostAdapterIp >> 16) & 0xFF)
                      << "." << ((hostAdapterIp >> 8) & 0xFF) << "." << (hostAdapterIp & 0xFF)
                      << std::endl;

            // Check if host adapter can reach camera (same subnet)
            if ((camIp & camMask) != (hostAdapterIp & camMask)) {
                std::cout << "[WARN] Camera " << devSerial
                          << " is on DIFFERENT subnet than host adapter! "
                          << "This may cause MV_CC_OpenDevice to fail." << std::endl;
            }

            if (devSerial == camConfig.serial) {
                matchedInfo = devList.pDeviceInfo[i];
                break;
            }
        }

        if (matchedInfo == nullptr) {
            std::cout << "[WARN] Camera " << camConfig.serial
                      << " not found in device list" << std::endl;
            m_cameras.push_back(std::move(camera));
            m_streamers.push_back(nullptr);
            continue;
        }

        bool cameraSuccess = camera->Initialize(matchedInfo);
        if (!cameraSuccess) {
            std::cout << "[WARN] Camera " << camConfig.serial << " initialization failed" << std::endl;
            m_cameras.push_back(std::move(camera));
            m_streamers.push_back(nullptr);
            continue;
        }

        m_cameras.push_back(std::move(camera));

        auto streamer = std::make_unique<GstRtspStreamer>(
            camConfig.rtsp_url, camConfig.width, camConfig.height, camConfig.fps);
        m_streamers.push_back(std::move(streamer));
    }

    if (m_cameras.empty()) {
        std::cout << "[ERROR] No cameras configured" << std::endl;
        m_status = Status::failed;
        return false;
    }

    m_status = Status::stopped;
    std::cout << "[SUCCESS] IngestEngine initialized with " << m_cameras.size() << " cameras" << std::endl;
    return true;
}

bool IngestEngine::Start() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_status == Status::running || m_status == Status::degraded) {
        std::cout << "[WARN] IngestEngine is already running" << std::endl;
        return true;
    }

    if (m_status != Status::stopped && m_status != Status::initializing) {
        std::cout << "[ERROR] IngestEngine not in valid state to start" << std::endl;
        return false;
    }

    m_running = true;
    size_t startedCount = 0;
    size_t streamerInitializedCount = 0;

    fprintf(stderr, "[ENGINE-DEBUG] Starting cameras...\n");
    fflush(stderr);

    for (size_t i = 0; i < m_cameras.size(); i++) {
        if (m_cameras[i]->IsOnline() && m_cameras[i]->StartGrabbing()) {
            startedCount++;
        }
    }

    fprintf(stderr, "[ENGINE-DEBUG] Cameras started=%zu. Initializing streamers...\n", startedCount);
    fflush(stderr);

    for (size_t i = 0; i < m_streamers.size(); i++) {
        if (m_streamers[i] == nullptr) continue;
        if (!m_cameras[i]->IsOnline()) continue;

        if (m_streamers[i]->Initialize()) {
            streamerInitializedCount++;
        } else {
            std::cout << "[ERROR] Streamer for camera " << i << " failed to initialize" << std::endl;
        }
    }

    fprintf(stderr, "[ENGINE-DEBUG] Streamers initialized=%zu. Starting streaming threads...\n", streamerInitializedCount);
    fflush(stderr);

    // Start streaming threads FIRST, so frames are being pushed into pipeline
    // before we wait for H.264 data to appear
    for (size_t i = 0; i < m_cameras.size(); i++) {
        m_streamingThreads.emplace_back(&IngestEngine::StreamingThread, this, i);
    }

    fprintf(stderr, "[ENGINE-DEBUG] Streaming threads started. Starting streamers...\n");
    fflush(stderr);

    // Now start each streamer (pipeline to PLAYING)
    for (size_t i = 0; i < m_streamers.size(); i++) {
        if (m_streamers[i] == nullptr) continue;
        fprintf(stderr, "[ENGINE-DEBUG] Starting streamer[%zu]...\n", i);
        fflush(stderr);
        if (m_streamers[i]->GetStatus() == GstRtspStreamer::Status::stopped) {
            m_streamers[i]->Start();
        }
        fprintf(stderr, "[ENGINE-DEBUG] Streamer[%zu] Start() returned.\n", i);
        fflush(stderr);
    }

    fprintf(stderr, "[ENGINE-DEBUG] All streamers started. Running final checks...\n");
    fflush(stderr);

    if (startedCount == 0) {
        std::cout << "[ERROR] No cameras started successfully" << std::endl;
        m_status = Status::failed;
        m_running = false;
        return false;
    }

    fprintf(stderr, "[ENGINE-DEBUG] Calling GetOnlineCameraCount()...\n");
    fflush(stderr);

    size_t onlineCount = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const auto& cam : m_cameras) {
            if (cam->IsOnline()) onlineCount++;
        }
    }

    fprintf(stderr, "[ENGINE-DEBUG] onlineCount=%zu\n", onlineCount);
    fflush(stderr);

    bool allStreaming = (streamerInitializedCount > 0) && 
                        (streamerInitializedCount == onlineCount);

    fprintf(stderr, "[ENGINE-DEBUG] allStreaming=%d. Setting status...\n", allStreaming ? 1 : 0);
    fflush(stderr);

    if (startedCount == m_cameras.size() && allStreaming) {
        m_status = Status::running;
    } else {
        m_status = Status::degraded;
    }

    fprintf(stderr, "[ENGINE-DEBUG] Start() about to print summary, startedCount=%zu, streamerInitializedCount=%zu, onlineCount=%zu\n", startedCount, streamerInitializedCount, onlineCount);
    fflush(stderr);

    std::cout << "[SUCCESS] IngestEngine started. " << startedCount << "/" << m_cameras.size() 
              << " cameras running, " << streamerInitializedCount << "/" << onlineCount
              << " streamers initialized" << std::endl;
    std::cout << std::flush;
    fprintf(stderr, "[ENGINE-DEBUG] Start() returning true\n");
    fflush(stderr);
    return true;
}

void IngestEngine::Stop() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_status == Status::stopped || m_status == Status::idle) {
        return;
    }

    m_running = false;

    for (auto& thread : m_streamingThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_streamingThreads.clear();

    for (auto& streamer : m_streamers) {
        if (streamer) {
            streamer->Stop();
        }
    }

    for (auto& camera : m_cameras) {
        camera->Stop();
        camera->Close();
    }

    m_status = Status::stopped;
    std::cout << "[INFO] IngestEngine stopped" << std::endl;
}

void IngestEngine::StreamingThread(size_t cameraIndex) {
    std::cout << "[INFO] Streaming thread started for camera " << cameraIndex << std::endl;

    while (m_running) {
        if (cameraIndex >= m_cameras.size() || cameraIndex >= m_streamers.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (m_streamers[cameraIndex] == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        FrameData frame;
        if (m_cameras[cameraIndex]->GetFrame(frame, 100)) {
            if (m_streamers[cameraIndex]->GetStatus() == GstRtspStreamer::Status::running) {
                GstFlowReturn ret = m_streamers[cameraIndex]->PushFrame(
                    frame.data, frame.frameInfo.nFrameLen);

                if (ret != GST_FLOW_OK) {
                    std::cout << "[ERROR] Camera " << cameraIndex << " PushFrame failed, ret=" << ret << std::endl;
                    if (m_streamers[cameraIndex]->HasError()) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "[INFO] Streaming thread stopped for camera " << cameraIndex << std::endl;
}

IngestEngine::Status IngestEngine::GetStatus() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_status;
}

std::vector<IngestEngine::CameraStatus> IngestEngine::GetCameraStatuses() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    std::vector<CameraStatus> statuses;

    for (size_t i = 0; i < m_cameras.size(); i++) {
        CameraStatus status;
        status.camera_id = m_cameras[i]->GetCameraId();
        status.role = m_cameras[i]->GetRole();
        status.online = m_cameras[i]->IsOnline();

        if (i < m_streamers.size() && m_streamers[i] != nullptr) {
            status.streaming = (m_streamers[i]->GetStatus() == GstRtspStreamer::Status::running);
        } else {
            status.streaming = false;
        }

        status.frame_count = m_cameras[i]->GetFrameCount();

        auto info = m_cameras[i]->GetCameraInfo();
        status.width = info.width;
        status.height = info.height;
        status.fps = info.fps;

        statuses.push_back(status);
    }

    return statuses;
}

size_t IngestEngine::GetOnlineCameraCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& camera : m_cameras) {
        if (camera->IsOnline()) {
            count++;
        }
    }

    return count;
}

size_t IngestEngine::GetTotalCameraCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_cameras.size();
}

void IngestEngine::CheckAndReconnect() {
    std::vector<size_t> cameraRestartIndices;
    std::vector<size_t> streamerRestartIndices;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (size_t i = 0; i < m_cameras.size(); i++) {
            const bool offline = !m_cameras[i]->IsOnline();
            const bool stale = m_cameras[i]->GetStatus() == CameraDevice::Status::running &&
                               m_cameras[i]->GetLastFrameTimeMs() > 15000;
            const bool reconnectDue =
                i >= m_lastReconnectAttempts.size() ||
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_lastReconnectAttempts[i]).count() >= 5000;
            if (offline || stale) {
                if (reconnectDue) {
                    if (m_lastReconnectAttempts.size() <= i) {
                        m_lastReconnectAttempts.resize(i + 1);
                    }
                    m_lastReconnectAttempts[i] = std::chrono::steady_clock::now();
                    cameraRestartIndices.push_back(i);
                }
                continue;
            }
            if (i < m_streamers.size() && m_streamers[i] != nullptr && m_streamers[i]->HasError()) {
                streamerRestartIndices.push_back(i);
            }
        }
    }

    for (size_t i : cameraRestartIndices) {
        std::cout << "[INFO] Camera " << m_cameras[i]->GetSerial()
                  << " is offline or stale (" << m_cameras[i]->GetLastFrameTimeMs()
                  << "ms since last frame), attempting reconnect..." << std::endl;

        if (m_cameras[i]->Reconnect()) {
            std::cout << "[INFO] Camera " << i << " reconnected, restarting streamer..." << std::endl;

            // Re-create streamer if missing (e.g. camera init failed on first run)
            bool needFresh = (i >= m_streamers.size()) || (m_streamers[i] == nullptr);
            if (needFresh && i < m_config.cameras.size()) {
                if (m_streamers.size() <= i) {
                    m_streamers.resize(i + 1);
                }
                const auto& cc = m_config.cameras[i];
                m_streamers[i] = std::make_unique<GstRtspStreamer>(
                    cc.rtsp_url, cc.width, cc.height, cc.fps);
                std::cout << "[INFO] Created new streamer for camera " << i
                          << " -> " << cc.rtsp_url << std::endl;
            }

            if (i < m_streamers.size() && m_streamers[i] != nullptr) {
                m_streamers[i]->Stop();
                if (m_streamers[i]->Initialize()) {
                    m_streamers[i]->Start();
                }
            }
        }
    }

    for (size_t i : streamerRestartIndices) {
        std::cout << "[INFO] Streamer for camera " << i << " has error, restarting pipeline..." << std::endl;
        if (i < m_streamers.size() && m_streamers[i] != nullptr) {
            m_streamers[i]->Stop();
            if (m_streamers[i]->Initialize()) {
                m_streamers[i]->Start();
            }
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        size_t onlineCount = 0;
        for (const auto& camera : m_cameras) {
            if (camera->IsOnline()) onlineCount++;
        }

        if (onlineCount > 0 && (!m_running.load() || m_streamingThreads.empty())) {
            m_running = false;
            for (auto& thread : m_streamingThreads) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            m_streamingThreads.clear();
            m_running = true;
            for (size_t i = 0; i < m_cameras.size(); i++) {
                m_streamingThreads.emplace_back(&IngestEngine::StreamingThread, this, i);
            }
            std::cout << "[INFO] Streaming threads restarted after camera recovery" << std::endl;
        }

        if (onlineCount == 0) {
            m_status = Status::failed;
        } else if (onlineCount < m_cameras.size()) {
            m_status = Status::degraded;
        } else {
            m_status = Status::running;
        }
    }
}

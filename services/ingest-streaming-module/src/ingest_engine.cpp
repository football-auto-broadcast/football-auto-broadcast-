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

    for (const auto& camConfig : config.cameras) {
        auto camera = std::make_unique<CameraDevice>(camConfig.serial, camConfig.camera_id, camConfig.role);
        bool cameraSuccess = camera->Initialize();

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
    std::vector<size_t> offlineIndices;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (size_t i = 0; i < m_cameras.size(); i++) {
            if (!m_cameras[i]->IsOnline()) {
                offlineIndices.push_back(i);
            }
        }
    }

    for (size_t i : offlineIndices) {
        std::cout << "[INFO] Camera " << m_cameras[i]->GetSerial() << " is offline, attempting reconnect..." << std::endl;

        if (m_cameras[i]->Reconnect()) {
            std::cout << "[INFO] Camera " << i << " reconnected, restarting streamer..." << std::endl;

            // Re-create streamer if missing (e.g. camera init failed on first run)
            bool needFresh = (i >= m_streamers.size()) || (m_streamers[i] == nullptr);
            if (needFresh && i < m_config.cameras.size()) {
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

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        size_t onlineCount = 0;
        for (const auto& camera : m_cameras) {
            if (camera->IsOnline()) onlineCount++;
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

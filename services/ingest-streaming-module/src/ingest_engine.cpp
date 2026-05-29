#include "ingest_engine.h"
#include <iostream>

IngestEngine::IngestEngine() {}

IngestEngine::~IngestEngine() {
    Stop();
}

bool IngestEngine::Initialize(const IngestConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::idle) {
        std::cout << "[ERROR] IngestEngine is not in idle state" << std::endl;
        return false;
    }
    
    m_status = Status::initializing;
    
    for (const auto& camConfig : config.cameras) {
        auto camera = std::make_unique<CameraDevice>(camConfig.serial, camConfig.camera_id, camConfig.role);
        bool success = camera->Initialize();
        
        if (!success) {
            std::cout << "[WARN] Camera " << camConfig.serial << " initialization failed, continuing with other cameras" << std::endl;
        }
        
        m_cameras.push_back(std::move(camera));
    }
    
    if (m_cameras.empty()) {
        std::cout << "[ERROR] No cameras initialized" << std::endl;
        m_status = Status::failed;
        return false;
    }
    
    m_status = Status::stopped;
    std::cout << "[SUCCESS] IngestEngine initialized with " << m_cameras.size() << " cameras" << std::endl;
    return true;
}

bool IngestEngine::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status == Status::running || m_status == Status::degraded) {
        std::cout << "[WARN] IngestEngine is already running" << std::endl;
        return true;
    }
    
    if (m_status != Status::stopped && m_status != Status::initializing) {
        std::cout << "[ERROR] IngestEngine not in valid state to start" << std::endl;
        return false;
    }
    
    size_t startedCount = 0;
    
    for (auto& camera : m_cameras) {
        if (camera->StartGrabbing()) {
            startedCount++;
        }
    }
    
    if (startedCount == 0) {
        std::cout << "[ERROR] No cameras started successfully" << std::endl;
        m_status = Status::failed;
        return false;
    }
    
    if (startedCount == m_cameras.size()) {
        m_status = Status::running;
    } else {
        m_status = Status::degraded;
    }
    
    std::cout << "[SUCCESS] IngestEngine started. " << startedCount << "/" << m_cameras.size() << " cameras running" << std::endl;
    return true;
}

void IngestEngine::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status == Status::stopped || m_status == Status::idle) {
        return;
    }
    
    for (auto& camera : m_cameras) {
        camera->Stop();
        camera->Close();
    }
    
    m_status = Status::stopped;
    std::cout << "[INFO] IngestEngine stopped" << std::endl;
}

IngestEngine::Status IngestEngine::GetStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::vector<IngestEngine::CameraStatus> IngestEngine::GetCameraStatuses() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<CameraStatus> statuses;
    
    for (const auto& camera : m_cameras) {
        CameraStatus status;
        status.camera_id = camera->GetCameraId();
        status.role = camera->GetRole();
        status.online = camera->IsOnline();
        status.frame_count = camera->GetFrameCount();
        
        auto info = camera->GetCameraInfo();
        status.width = info.width;
        status.height = info.height;
        status.fps = info.fps;
        
        statuses.push_back(status);
    }
    
    return statuses;
}

size_t IngestEngine::GetOnlineCameraCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t count = 0;
    for (const auto& camera : m_cameras) {
        if (camera->IsOnline()) {
            count++;
        }
    }
    
    return count;
}

size_t IngestEngine::GetTotalCameraCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cameras.size();
}

void IngestEngine::CheckAndReconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t onlineCount = 0;
    for (auto& camera : m_cameras) {
        if (!camera->IsOnline()) {
            std::cout << "[INFO] Camera " << camera->GetSerial() << " is offline, attempting reconnect..." << std::endl;
            camera->Reconnect();
        }
        if (camera->IsOnline()) {
            onlineCount++;
        }
    }
    
    if (onlineCount == 0) {
        m_status = Status::failed;
    } else if (onlineCount < m_cameras.size()) {
        m_status = Status::degraded;
    } else {
        m_status = Status::running;
    }
}
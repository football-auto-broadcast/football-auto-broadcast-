#pragma once

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include "camera_device.h"

struct CameraConfig {
    std::string serial;
    int camera_id;
    std::string role;
    int width = 1920;
    int height = 1080;
    double fps = 25.0;
};

struct IngestConfig {
    std::vector<CameraConfig> cameras;
    std::string data_root;
    std::string log_level = "info";
};

class IngestEngine {
public:
    enum class Status {
        idle,
        initializing,
        running,
        degraded,
        stopped,
        failed
    };

    IngestEngine();
    ~IngestEngine();

    bool Initialize(const IngestConfig& config);
    bool Start();
    void Stop();
    
    Status GetStatus() const;
    
    struct CameraStatus {
        int camera_id;
        std::string role;
        bool online;
        uint64_t frame_count;
        int width;
        int height;
        double fps;
    };
    
    std::vector<CameraStatus> GetCameraStatuses() const;
    
    size_t GetOnlineCameraCount() const;
    size_t GetTotalCameraCount() const;
    void CheckAndReconnect();

private:
    std::vector<std::unique_ptr<CameraDevice>> m_cameras;
    Status m_status = Status::idle;
    mutable std::mutex m_mutex;
};
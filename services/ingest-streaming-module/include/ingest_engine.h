#pragma once

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include "camera_device.h"
#include "gst_rtsp_streamer.h"

struct CameraConfig {
    std::string serial;
    int camera_id;
    std::string role;
    int width = 2592;
    int height = 1944;
    double fps = 25.0;
    std::string rtsp_url;
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
        bool streaming;
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
    void StreamingThread(size_t cameraIndex);
    
    std::vector<std::unique_ptr<CameraDevice>> m_cameras;
    std::vector<std::unique_ptr<GstRtspStreamer>> m_streamers;
    std::vector<std::thread> m_streamingThreads;
    std::atomic<bool> m_running{false};
    Status m_status = Status::idle;
    mutable std::mutex m_mutex;
};
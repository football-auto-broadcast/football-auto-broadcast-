#pragma once

#include <string>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <memory>
#include "MvCameraControl.h"

struct FrameData {
    unsigned char* data;
    MV_FRAME_OUT_INFO_EX frameInfo;
    std::chrono::steady_clock::time_point timestamp;
    
    FrameData() : data(nullptr) {
        memset(&frameInfo, 0, sizeof(MV_FRAME_OUT_INFO_EX));
    }
    
    FrameData(unsigned char* d, const MV_FRAME_OUT_INFO_EX& info) 
        : data(d), frameInfo(info), timestamp(std::chrono::steady_clock::now()) {}
    
    ~FrameData() {
        if (data != nullptr) {
            delete[] data;
        }
    }
    
    FrameData(const FrameData&) = delete;
    FrameData& operator=(const FrameData&) = delete;
    
    FrameData(FrameData&& other) noexcept 
        : data(other.data), frameInfo(other.frameInfo), timestamp(other.timestamp) {
        other.data = nullptr;
    }
    
    FrameData& operator=(FrameData&& other) noexcept {
        if (this != &other) {
            delete[] data;
            data = other.data;
            frameInfo = other.frameInfo;
            timestamp = other.timestamp;
            other.data = nullptr;
        }
        return *this;
    }
};

class CameraDevice {
public:
    enum class Status {
        idle,
        initializing,
        running,
        stopped,
        failed
    };

    CameraDevice(const std::string& serial, int cameraId, const std::string& role);
    ~CameraDevice();

    bool Initialize();
    bool Open();
    bool StartGrabbing();
    void Stop();
    void Close();
    bool Reconnect();
    
    Status GetStatus() const;
    const std::string& GetSerial() const { return m_serial; }
    int GetCameraId() const { return m_cameraId; }
    const std::string& GetRole() const { return m_role; }
    uint64_t GetFrameCount() const;
    bool IsOnline() const;
    int64_t GetLastFrameTimeMs() const;
    
    bool GetFrame(FrameData& frame, int timeoutMs = 100);
    void ClearFrameQueue();
    
    struct CameraInfo {
        int width = 0;
        int height = 0;
        double fps = 0.0;
    };
    CameraInfo GetCameraInfo() const;

private:
    static void __stdcall ImageCallbackEx(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser);
    void OnFrameReceived(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo);
    
    std::string m_serial;
    int m_cameraId;
    std::string m_role;
    
    void* m_cameraHandle = nullptr;
    Status m_status = Status::idle;
    
    mutable std::recursive_mutex m_mutex;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_cv;
    
    std::queue<std::unique_ptr<FrameData>> m_frameQueue;
    static constexpr size_t MAX_QUEUE_SIZE = 3;
    
    uint64_t m_frameCount = 0;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    
    CameraInfo m_cameraInfo;
    bool m_isOnline = false;
    
    bool GetCameraInfoFromSDK();
};
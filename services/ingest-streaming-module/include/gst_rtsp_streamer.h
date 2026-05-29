#pragma once

#include <string>
#include <memory>
#include <mutex>

#ifdef __cplusplus
extern "C" {
#endif
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#ifdef __cplusplus
}
#endif

class GstRtspStreamer {
public:
    enum class Status {
        idle,
        initializing,
        running,
        stopped,
        error
    };

    GstRtspStreamer(const std::string& rtspUrl, int width, int height, double fps);
    ~GstRtspStreamer();

    bool Initialize();
    bool Start();
    void Stop();
    
    GstFlowReturn PushFrame(unsigned char* data, size_t size);
    
    Status GetStatus() const;
    const std::string& GetRtspUrl() const { return m_rtspUrl; }

private:
    std::string m_rtspUrl;
    int m_width;
    int m_height;
    double m_fps;
    
    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    
    Status m_status = Status::idle;
    mutable std::mutex m_mutex;
    
    bool CreatePipeline();
    std::string BuildPipelineString();
};
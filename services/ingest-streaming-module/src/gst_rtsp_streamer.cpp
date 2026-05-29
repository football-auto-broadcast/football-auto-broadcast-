#include "gst_rtsp_streamer.h"
#include <iostream>
#include <sstream>

static bool g_gstInitialized = false;

GstRtspStreamer::GstRtspStreamer(const std::string& rtspUrl, int width, int height, double fps)
    : m_rtspUrl(rtspUrl), m_width(width), m_height(height), m_fps(fps) {
    if (!g_gstInitialized) {
        gst_init(nullptr, nullptr);
        g_gstInitialized = true;
        std::cout << "[INFO] GStreamer initialized" << std::endl;
    }
}

GstRtspStreamer::~GstRtspStreamer() {
    Stop();
}

bool GstRtspStreamer::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::idle) {
        std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl << " is not in idle state" << std::endl;
        return false;
    }
    
    m_status = Status::initializing;
    
    if (!CreatePipeline()) {
        m_status = Status::error;
        return false;
    }
    
    m_status = Status::stopped;
    std::cout << "[SUCCESS] GstRtspStreamer " << m_rtspUrl << " initialized" << std::endl;
    return true;
}

bool GstRtspStreamer::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::stopped) {
        std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl << " is not in stopped state" << std::endl;
        return false;
    }
    
    if (m_pipeline == nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " pipeline is null" << std::endl;
        return false;
    }
    
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " failed to start pipeline" << std::endl;
        m_status = Status::error;
        return false;
    }
    
    m_status = Status::running;
    std::cout << "[SUCCESS] GstRtspStreamer " << m_rtspUrl << " started" << std::endl;
    return true;
}

void GstRtspStreamer::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status == Status::idle) {
        return;
    }
    
    if (m_pipeline != nullptr) {
        gst_element_send_event(m_pipeline, gst_event_new_eos());
        
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_NULL);
        if (ret != GST_STATE_CHANGE_SUCCESS) {
            std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl << " failed to stop pipeline" << std::endl;
        }
        
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc = nullptr;
    }
    
    m_status = Status::idle;
    std::cout << "[INFO] GstRtspStreamer " << m_rtspUrl << " stopped" << std::endl;
}

GstFlowReturn GstRtspStreamer::PushFrame(unsigned char* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::running) {
        std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl << " is not running" << std::endl;
        return GST_FLOW_ERROR;
    }
    
    if (m_appsrc == nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " appsrc is null" << std::endl;
        return GST_FLOW_ERROR;
    }
    
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (buffer == nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " failed to allocate buffer" << std::endl;
        return GST_FLOW_ERROR;
    }
    
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        memcpy(map.data, data, size);
        gst_buffer_unmap(buffer, &map);
    } else {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " failed to map buffer" << std::endl;
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }
    
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);
    
    if (ret != GST_FLOW_OK) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " PushFrame failed, flow return: " << ret << std::endl;
        return GST_FLOW_ERROR;
    }
    
    return GST_FLOW_OK;
}

GstRtspStreamer::Status GstRtspStreamer::GetStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

bool GstRtspStreamer::CreatePipeline() {
    std::string pipelineStr = BuildPipelineString();
    std::cout << "[INFO] GStreamer pipeline: " << pipelineStr << std::endl;
    
    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.c_str(), &error);
    
    if (error != nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " failed to create pipeline: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }
    
    m_appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "mysrc");
    if (m_appsrc == nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " failed to get appsrc element" << std::endl;
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        return false;
    }
    
    gst_app_src_set_stream_type(GST_APP_SRC(m_appsrc), GST_APP_STREAM_TYPE_STREAM);
    gst_app_src_set_format(GST_APP_SRC(m_appsrc), GST_FORMAT_TIME);
    
    return true;
}

std::string GstRtspStreamer::BuildPipelineString() {
    std::ostringstream oss;
    oss << "appsrc name=mysrc caps=video/x-raw,format=RGB,width=" << m_width 
        << ",height=" << m_height << ",framerate=" << (int)m_fps << "/1 ! "
        << "videoconvert ! "
        << "videoscale ! "
        << "video/x-raw,format=I420,width=1920,height=1080,framerate=" << (int)m_fps << "/1 ! "
        << "x264enc tune=zerolatency speed-preset=ultrafast bitrate=8000 key-int-max=" << (int)m_fps << " ! "
        << "h264parse ! "
        << "rtspclientsink location=" << m_rtspUrl;
    
    return oss.str();
}
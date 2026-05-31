#include "gst_rtsp_streamer.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>

static bool g_gstInitialized = false;

GstRtspStreamer::GstRtspStreamer(const std::string& rtspUrl, int width, int height, double fps)
    : m_rtspUrl(rtspUrl), m_width(width), m_height(height), m_fps(fps),
      m_pipeline(nullptr), m_appsrc(nullptr), m_status(Status::idle),
      m_frameCount(0), m_baseTimestamp(0), m_busThreadRunning(false), m_pipelineError(false) {
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
    m_pipelineError = false;
    m_frameCount = 0;

    if (!CreatePipeline()) {
        m_status = Status::error;
        return false;
    }

    if (m_appsrc) {
        GstCaps* caps = gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "RGB",
            "width", G_TYPE_INT, m_width,
            "height", G_TYPE_INT, m_height,
            "framerate", GST_TYPE_FRACTION, static_cast<int>(m_fps + 0.5), 1,
            nullptr);
        gst_app_src_set_caps(GST_APP_SRC(m_appsrc), caps);
        gst_caps_unref(caps);

        g_object_set(G_OBJECT(m_appsrc),
            "stream-type", GST_APP_STREAM_TYPE_STREAM,
            "format", GST_FORMAT_TIME,
            "is-live", TRUE,
            "block", FALSE,
            "max-bytes", static_cast<uint64_t>(m_width) * m_height * 3 * 2,
            nullptr);
    }

    m_status = Status::stopped;
    std::cout << "[SUCCESS] GstRtspStreamer " << m_rtspUrl << " initialized" << std::endl;
    return true;
}

bool GstRtspStreamer::Start() {
    std::unique_lock<std::mutex> lock(m_mutex);

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

    m_busThreadRunning = true;
    lock.unlock();

    m_busThread = std::thread([this]() {
        GstBus* bus = gst_element_get_bus(m_pipeline);
        if (!bus) return;

        while (m_busThreadRunning) {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
                (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (!msg) continue;

            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* err = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(msg, &err, &debug);
                std::cerr << "[ERROR] GstRtspStreamer " << m_rtspUrl 
                          << " pipeline error: " << err->message << std::endl;
                if (debug) g_free(debug);
                g_error_free(err);
                m_pipelineError = true;
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                std::cout << "[INFO] GstRtspStreamer " << m_rtspUrl << " received EOS" << std::endl;
            }
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    });

    lock.lock();
    m_status = Status::running;
    std::cout << "[SUCCESS] GstRtspStreamer " << m_rtspUrl << " started" << std::endl;
    return true;
}

void GstRtspStreamer::Stop() {
    m_busThreadRunning = false;
    if (m_busThread.joinable()) {
        m_busThread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_status == Status::idle) {
        return;
    }

    if (m_pipeline != nullptr) {
        if (m_appsrc) {
            gst_app_src_end_of_stream(GST_APP_SRC(m_appsrc));
        }

        gst_element_send_event(m_pipeline, gst_event_new_eos());

        GstBus* bus = gst_element_get_bus(m_pipeline);
        if (bus) {
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 500 * GST_MSECOND, GST_MESSAGE_EOS);
            if (msg) gst_message_unref(msg);
            gst_object_unref(bus);
        }

        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl << " failed to set NULL state" << std::endl;
        }

        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc = nullptr;
    }

    m_status = Status::idle;
    m_frameCount = 0;
    std::cout << "[INFO] GstRtspStreamer " << m_rtspUrl << " stopped" << std::endl;
}

GstFlowReturn GstRtspStreamer::PushFrame(unsigned char* data, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_status != Status::running) {
        return GST_FLOW_ERROR;
    }

    if (m_pipelineError) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl << " pipeline has error" << std::endl;
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

    GstClockTime frameDuration = GST_SECOND / static_cast<GstClockTime>(m_fps + 0.5);
    GstClockTime pts = m_frameCount * frameDuration;
    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = frameDuration;
    m_frameCount++;

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
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl 
                  << " PushFrame failed, flow return: " << ret << std::endl;
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

GstRtspStreamer::Status GstRtspStreamer::GetStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

bool GstRtspStreamer::HasError() const {
    return m_pipelineError.load();
}

bool GstRtspStreamer::CreatePipeline() {
    std::string pipelineStr = BuildPipelineString();
    std::cout << "[INFO] GStreamer pipeline: " << pipelineStr << std::endl;

    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.c_str(), &error);

    if (error != nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl 
                  << " failed to create pipeline: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }

    m_appsrc = gst_bin_get_by_name(GST_BIN(m_pipeline), "mysrc");
    if (m_appsrc == nullptr) {
        std::cout << "[ERROR] GstRtspStreamer " << m_rtspUrl 
                  << " failed to get appsrc element" << std::endl;
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        return false;
    }

    return true;
}

std::string GstRtspStreamer::BuildPipelineString() {
    std::ostringstream oss;
    oss << "appsrc name=mysrc caps=video/x-raw,format=RGB,width=" << m_width 
        << ",height=" << m_height << ",framerate=" << static_cast<int>(m_fps + 0.5) << "/1 ! "
        << "videoconvert ! "
        << "videoscale ! "
        << "video/x-raw,format=I420,width=1920,height=1080,framerate=" << static_cast<int>(m_fps + 0.5) << "/1 ! "
        << "x264enc tune=zerolatency speed-preset=ultrafast bitrate=8000 key-int-max=" << static_cast<int>(m_fps + 0.5) << " ! "
        << "h264parse ! "
        << "rtspclientsink location=" << m_rtspUrl;

    return oss.str();
}

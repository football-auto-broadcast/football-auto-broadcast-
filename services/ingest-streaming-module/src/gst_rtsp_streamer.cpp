#include "gst_rtsp_streamer.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <string>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

static std::once_flag g_gstInitFlag;

static void InitGstreamerEnv() {
#ifdef _WIN32
    // Get executable directory (e.g. "D:\..\bin\")
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0) {
        std::string exeDir(exePath, len);
        size_t pos = exeDir.find_last_of("\\/");
        if (pos != std::string::npos) {
            exeDir = exeDir.substr(0, pos);
            // GST_PLUGIN_PATH = <exeDir>\lib\gstreamer-1.0
            std::string pluginPath = exeDir + "\\lib\\gstreamer-1.0";
            _putenv_s("GST_PLUGIN_PATH", pluginPath.c_str());
            // Also make sure bin directory is in PATH for DLL loading
            std::string currentPath;
            char* existingPath = std::getenv("PATH");
            if (existingPath) currentPath = existingPath;
            if (currentPath.find(exeDir) == std::string::npos) {
                std::string newPath = exeDir + ";" + currentPath;
                _putenv_s("PATH", newPath.c_str());
            }
        }
    }
#endif
}

GstRtspStreamer::GstRtspStreamer(const std::string& rtspUrl, int width, int height, double fps)
    : m_rtspUrl(rtspUrl), m_width(width), m_height(height), m_fps(fps),
      m_pipeline(nullptr), m_appsrc(nullptr), m_status(Status::idle),
      m_frameCount(0), m_baseTimestamp(0), m_busThreadRunning(false), m_pipelineError(false) {
    std::call_once(g_gstInitFlag, []() {
        InitGstreamerEnv();
        gst_init(nullptr, nullptr);
        std::cout << "[INFO] GStreamer initialized (GST_PLUGIN_PATH set from exe dir)" << std::endl;
    });
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

    // Wait for pipeline to reach PLAYING state
    GstState state;
    gst_element_get_state(m_pipeline, &state, nullptr, 2 * GST_SECOND);

    m_status = Status::running;
    std::cout << "[SUCCESS] GstRtspStreamer " << m_rtspUrl << " pipeline started" << std::endl;

    m_busThreadRunning = true;
    lock.unlock();

    // rtspclientsink handles the RTSP connection directly to MediaMTX
    // No separate FFmpeg process needed - GStreamer pushes H.264 directly

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
        return GST_FLOW_ERROR;
    }

    if (m_appsrc == nullptr) {
        return GST_FLOW_ERROR;
    }

    if (data == nullptr || size == 0) {
        return GST_FLOW_ERROR;
    }

    // Validate buffer size matches expected RGB24 frame size
    // width * height * 3 (bytes per pixel for RGB)
    size_t expectedSize = static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 3;
    if (size != expectedSize) {
        std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl
                  << " frame size mismatch: got " << size << " bytes, expected " << expectedSize
                  << " bytes (RGB24 " << m_width << "x" << m_height << ")" << std::endl;
        // Skip frames that don't match expected size - pixel format conversion needed upstream
        return GST_FLOW_OK;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (buffer == nullptr) {
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
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), buffer);

    if (ret != GST_FLOW_OK) {
        std::cout << "[WARN] GstRtspStreamer " << m_rtspUrl
                  << " push_buffer returned " << ret << " (frame " << m_frameCount << ")" << std::endl;
        return GST_FLOW_ERROR;
    }

    // Log first push, then every 250 frames to avoid spam
    if (m_frameCount == 1 || m_frameCount % 250 == 0) {
        std::cout << "[INFO] " << m_rtspUrl << " pushed " << m_frameCount
                  << " frames (" << (size / 1024) << "KB each) to pipeline" << std::endl;
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
        std::cout << "[ERROR] GstRtspStreamer "
                  << " failed to get appsrc element" << std::endl;
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        return false;
    }

    // Configure appsrc for live mode with camera input
    // do-timestamp=true: timestamps are assigned based on arrival time
    // block=false: push() never blocks; if queue is full, buffers are dropped
    // This is correct for live camera streaming where we want latest frames
    g_object_set(G_OBJECT(m_appsrc),
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "do-timestamp", TRUE,
        "block", FALSE,
        nullptr);

    return true;
}

std::string GstRtspStreamer::BuildPipelineString() {
    std::ostringstream oss;

    // Pipeline: appsrc -> videoconvert -> videocrop(16:9) -> videoscale -> x264enc -> rtspclientsink
    //
    // Contract §8.1 requires RTSP output at 1920x1080@25fps
    // Camera is 2592x1944 (4:3 aspect ratio)
    //
    // To avoid aspect-ratio distortion when converting 4:3 to 16:9:
    //   1. Crop 2592x1944 to 16:9 region (center crop: crop 176px top+bottom)
    //      Result: 2592x1459 (still 4:3 source but taller than 16:9 crop)
    //   2. Scale cropped region to 1920x1080
    //
    // This gives proper 16:9 output without stretching the image.
    //
    // Quality settings:
    //   - bitrate=20000: 20 Mbps for sharp detail
    //   - speed-preset=medium: better quality than ultrafast
    //   - qp-min=10 / qp-max=30: constrain quality range
    //   - tune=zerolatency: keep low latency for live streaming
    //   - config-interval=1: send SPS/PPS with every IDR frame

    // Target output resolution per contract
    int out_width = 1920;
    int out_height = 1080;

    // Source resolution from camera
    int in_width = m_width;
    int in_height = m_height;

    // Calculate crop values for 16:9 aspect ratio
    // crop_vertical = (in_height - in_width * 9 / 16) / 2
    // For 2592x1944: (1944 - 2592*9/16) / 2 = (1944 - 1459.5) / 2 = 242.25 -> use 242
    int crop_vert = (in_height - in_width * 9 / 16) / 2;
    if (crop_vert < 0) crop_vert = 0;  // In case already 16:9 or wider

    oss << "appsrc name=mysrc caps=video/x-raw,format=RGB,width=" << in_width
        << ",height=" << in_height << ",framerate=" << static_cast<int>(m_fps + 0.5) << "/1 ! "
        << "videoconvert ! "
        << "video/x-raw,format=I420,width=" << in_width << ",height=" << in_height
        << ",framerate=" << static_cast<int>(m_fps + 0.5) << "/1 ! ";

    // Add cropping if source is not 16:9
    if (crop_vert > 0) {
        oss << "videocrop top=" << crop_vert << " bottom=" << crop_vert << " ! ";
    }

    oss << "videoscale ! "
        << "video/x-raw,width=" << out_width << ",height=" << out_height
        << ",framerate=" << static_cast<int>(m_fps + 0.5) << "/1 ! "
        << "x264enc tune=zerolatency speed-preset=medium bitrate=20000 qp-min=18 qp-max=30 key-int-max=50 ! "
        << "video/x-h264,profile=baseline,stream-format=byte-stream ! "
        << "rtspclientsink protocols=tcp latency=200 location=" << m_rtspUrl;

    return oss.str();
}

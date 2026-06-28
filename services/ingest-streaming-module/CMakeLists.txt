# ============================================================================
# CMakeLists.txt — 采集与编码分发模块 (ingest-streaming-module)
# 模块 A：海康 GigE 双相机取流 + HTTP 控制面 + 本机 RTSP 分发
#
# 团队协作规范（极度重要）：
#   1. 绝对不要写死海康 SDK 的绝对路径
#   2. 使用缓存变量 MVS_SDK_ROOT，队友换电脑只需改此一处
#   3. 所有依赖路径必须通过 ${MVS_SDK_ROOT} 拼接
#   4. 正确链接 MvCameraControl.lib
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(ingest_streaming_module LANGUAGES CXX)

# ---- C++17 标准 ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- 编译选项 ----
if(MSVC)
    add_compile_options(/W4 /WX- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# 【极度重要】海康 MVS SDK 路径 — 缓存变量，禁止写死
# ============================================================================
set(MVS_SDK_ROOT "C:/Program Files (x86)/MVS/Development"
    CACHE PATH "Directory of Hikvision MVS SDK (e.g. C:/Program Files (x86)/MVS/Development)")

# ---- SDK 头文件路径（全部通过 MVS_SDK_ROOT 拼接）----
set(MVS_INCLUDE_DIR "${MVS_SDK_ROOT}/Includes")

# ---- SDK 库文件路径（全部通过 MVS_SDK_ROOT 拼接）----
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(MVS_LIB_DIR "${MVS_SDK_ROOT}/Libraries/win64")
    else()
        set(MVS_LIB_DIR "${MVS_SDK_ROOT}/Libraries/win32")
    endif()
endif()

# ---- 验证 SDK 路径是否存在 ----
if(NOT EXISTS "${MVS_INCLUDE_DIR}/MvCameraControl.h")
    message(FATAL_ERROR
        "MvCameraControl.h not found at: ${MVS_INCLUDE_DIR}\n"
        "Please set MVS_SDK_ROOT to your Hikvision MVS Development directory.\n"
        "Example: cmake -DMVS_SDK_ROOT=\"D:/MVS/Development\" ..")
endif()

if(NOT EXISTS "${MVS_LIB_DIR}/MvCameraControl.lib")
    message(FATAL_ERROR
        "MvCameraControl.lib not found at: ${MVS_LIB_DIR}\n"
        "Ensure MVS SDK is installed and MVS_SDK_ROOT is set correctly.")
endif()

message(STATUS "MVS SDK Root : ${MVS_SDK_ROOT}")
message(STATUS "MVS Include  : ${MVS_INCLUDE_DIR}")
message(STATUS "MVS Lib      : ${MVS_LIB_DIR}")

# ---- FFmpeg executable used by the RTSP/RTMP publishing worker ----
set(FFMPEG_EXE_PATH "D:/ffmpeg/ffmpeg/bin/ffmpeg.exe"
    CACHE FILEPATH "Path to ffmpeg executable used by ingest streaming service")

message(STATUS "FFmpeg exe    : ${FFMPEG_EXE_PATH}")

# ============================================================================
# OpenCV — 图像处理与视频编码（Milestone 3 核心依赖）
# 用于 cv::VideoWriter（FFmpeg 后端）→ H.264 编码 → RTSP 推流
# 与 vision-event-module 使用同一套 OpenCV 安装
# ============================================================================
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc videoio)
message(STATUS "OpenCV found   : ${OpenCV_VERSION}")
message(STATUS "OpenCV includes: ${OpenCV_INCLUDE_DIRS}")

# ============================================================================
# 第三方依赖（通过 FetchContent 自动下载，无需手动复制）
# ============================================================================

include(FetchContent)

# 避免反复下载：设置本地缓存根目录（可选，加快多模块共用）
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps"
    CACHE PATH "FetchContent download root")
set(FETCHCONTENT_UPDATES_DISCONNECTED ON
    CACHE BOOL "Do not update already-populated FetchContent dependencies during local builds")

# ---- spdlog — 异步日志库（header-only + 少量编译单元）----
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# ---- cpp-httplib — 单头文件 HTTP/HTTPS 库（Milestone 2 核心依赖）----
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.1
)
FetchContent_MakeAvailable(httplib)

# ---- nlohmann/json — 单头文件 JSON 解析库（Milestone 2 核心依赖）----
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ============================================================================
# 模块源文件
# ============================================================================

set(MODULE_SOURCES
    src/main.cpp
    src/camera_controller.cpp
    src/camera_manager.cpp
    src/http_server.cpp
)

set(MODULE_HEADERS
    include/frame_data.h
    include/stream_config.h
    include/camera_controller.h
    include/camera_manager.h
    include/http_server.h
)

# ============================================================================
# 可执行目标
# ============================================================================

add_executable(ingest_streaming_service ${MODULE_SOURCES} ${MODULE_HEADERS})

# ---- 头文件搜索路径 ----
target_include_directories(ingest_streaming_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${MVS_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

# ---- 库文件搜索路径 ----
target_link_directories(ingest_streaming_service PRIVATE
    ${MVS_LIB_DIR}
)

# ---- 链接库 ----
target_link_libraries(ingest_streaming_service PRIVATE
    # -------- OpenCV（Milestone 3 核心依赖）--------
    ${OpenCV_LIBS}

    # -------- 海康 SDK（必须）--------
    MvCameraControl

    # -------- HTTP REST（Milestone 2 启用）--------
    httplib::httplib

    # -------- JSON 解析（Milestone 2 启用）--------
    nlohmann_json::nlohmann_json

    # -------- 日志（必须）--------
    spdlog::spdlog

    # -------- Windows 系统库（必须）--------
    ws2_32
    crypt32
)

# ---- 编译定义 ----
target_compile_definitions(ingest_streaming_service PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    INGEST_DEFAULT_FFMPEG_EXE_PATH="${FFMPEG_EXE_PATH}"
    $<$<CONFIG:Debug>:INGEST_DEBUG>
)

# ---- Windows 特定：httplib 需要 winsock ----
if(WIN32)
    # ws2_32 已在上方链接
endif()

# ============================================================================
# 输出目录
# ============================================================================

set_target_properties(ingest_streaming_service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ============================================================================
# 安装规则
# ============================================================================

install(TARGETS ingest_streaming_service DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/ingest-streaming-module OPTIONAL)

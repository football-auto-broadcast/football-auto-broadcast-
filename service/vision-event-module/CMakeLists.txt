# CMakeLists.txt - CMake 构建配置文件
#
# 定义依赖、编译选项和安装规则。
# 构建视觉分析模块 (vision_event_service)。

cmake_minimum_required(VERSION 3.16)
project(vision_event_module LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# 依赖查找
# ============================================================================

# OpenCV
find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc highgui videoio)
message(STATUS "OpenCV found: ${OpenCV_VERSION}")

# ONNX Runtime
find_package(onnxruntime REQUIRED)
message(STATUS "ONNX Runtime found: ${onnxruntime_VERSION}")

# ============================================================================
# 头文件包含目录
# ============================================================================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/inference/include
    ${OpenCV_INCLUDE_DIRS}
)

# ============================================================================
# 核心模块源文件
# ============================================================================

set(CORE_SOURCES
    src/main.cpp
    src/service.cpp
    src/http_server.cpp
    src/event_types.cpp
    src/focus_region.cpp
    src/multi_focus_region.cpp
    src/program_decision.cpp
    src/json_output.cpp
    src/camera_role.cpp
    src/fusion_policy.cpp
)

# ============================================================================
# 推理模块源文件
# ============================================================================

set(INFERENCE_SOURCES
    inference/src/ball_detector.cpp
    inference/src/motion_analyzer.cpp
    inference/src/event_classifier.cpp
    inference/src/goal_assist_analyzer.cpp
    inference/src/box_activity_analyzer.cpp
)

# ============================================================================
# 可执行文件
# ============================================================================

add_executable(vision_event_service
    ${CORE_SOURCES}
    ${INFERENCE_SOURCES}
)

target_link_libraries(vision_event_service
    ${OpenCV_LIBS}
    onnxruntime
)

# ============================================================================
# 测试
# ============================================================================

enable_testing()

set(TEST_SOURCES
    tests/test_event_types.cpp
    tests/test_focus_region.cpp
    tests/test_multi_focus_region.cpp
    tests/test_fusion_policy.cpp
)

# TODO: 配置测试框架 (Google Test / Catch2)
# add_executable(vision_event_tests ${CORE_SOURCES} ${TEST_SOURCES})
# target_link_libraries(vision_event_tests ${OpenCV_LIBS} onnxruntime GTest::GTest GTest::Main)
# add_test(NAME VisionEventTests COMMAND vision_event_tests)

# ============================================================================
# 安装规则
# ============================================================================

install(TARGETS vision_event_service DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/vision-event-module)

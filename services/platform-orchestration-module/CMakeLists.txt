cmake_minimum_required(VERSION 3.16)
project(platform_orchestration_module)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 指定可执行文件
add_executable(platform_orchestration_service
    src/main.cpp
)

# 添加头文件搜索路径（指向我们手动下载的库）
target_include_directories(platform_orchestration_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/json
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog
)

# spdlog 需要定义这个宏才能正常编译
#target_compile_definitions(platform_orchestration_service PRIVATE SPDLOG_FMT_EXTERNAL)

# 设置输出目录
set_target_properties(platform_orchestration_service PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
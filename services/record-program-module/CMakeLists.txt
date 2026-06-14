cmake_minimum_required(VERSION 3.14)
project(RecordProgramModule)

# 强制 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 自动链接 OpenCV
set(OpenCV_DIR "D:/opencv_mingw/OpenCV-MinGW-Build-OpenCV-4.5.5-x64")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 2. 引入本地 src 目录为头文件搜索路径 (完全跳过网络克隆)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 3. 定义生成目标（包含我们下载好的本地头文件）
add_executable(record_program_service 
    src/main.cpp 
    src/httplib.h
    src/json.hpp
    config.json
)

# 4. 链接所有依赖（只保留本地基础库，完美绕开网络库链接）
target_link_libraries(record_program_service 
    PRIVATE 
    ${OpenCV_LIBS} 
    ws2_32 crypt32
)

# 5. 构建后自动拷贝配置文件和 ffmpeg.exe 到编译输出目录
add_custom_command(TARGET record_program_service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:record_program_service>/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg.exe"
        "$<TARGET_FILE_DIR:record_program_service>/ffmpeg.exe"
)
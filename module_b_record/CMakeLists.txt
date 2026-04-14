cmake_minimum_required(VERSION 3.10)
project(ModuleB_Record)

# --- 🚀 组长，请务必确认文件夹名字是 opencv_mingw (带下划线) ---
set(OpenCV_DIR "D:/opencv_mingw/OpenCV-MinGW-Build-OpenCV-4.5.5-x64")

set(CMAKE_CXX_STANDARD 17)

# 注意：这些词之间必须用 _ 连接，不能是空格
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 生成程序： record_module 之间也建议加下划线
add_executable(record_module src/main.cpp)
target_link_libraries(record_module ${OpenCV_LIBS})
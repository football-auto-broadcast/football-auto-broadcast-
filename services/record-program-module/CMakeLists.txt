cmake_minimum_required(VERSION 3.14)
project(RecordProgramModule)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 链接 OpenCV
set(OpenCV_DIR "D:/opencv_mingw/OpenCV-MinGW-Build-OpenCV-4.5.5-x64")
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 2. 自动拉取网络库
include(FetchContent)
FetchContent_Declare(httplib GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.15.3)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_MakeAvailable(httplib json)

# 3. 定义生成目标
add_executable(record_program_service src/main.cpp)

# 4. 链接库
target_link_libraries(record_program_service PRIVATE ${OpenCV_LIBS} httplib::httplib nlohmann_json::nlohmann_json ws2_32 crypt32)
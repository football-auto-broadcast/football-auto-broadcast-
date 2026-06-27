cmake_minimum_required(VERSION 3.16)
project(highlight_generation_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(highlight_generation_service
    src/main.cpp
    src/http_server.cpp
    src/json_utils.cpp
    src/pipeline.cpp
)

target_include_directories(highlight_generation_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(highlight_generation_service PRIVATE ws2_32)
endif()

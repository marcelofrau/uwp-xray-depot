cmake_minimum_required(VERSION 3.15)
project(xray-sock LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXTERNAL "${CMAKE_CURRENT_SOURCE_DIR}/../external")

add_library(xray-sock STATIC
    xray-sock/tcp_listener.cpp
    xray-sock/tcp_listener.h
    xray-sock/safe_queue.h
    xb-xray/inspector.cpp
    xb-xray/uwp_sink.h
    xb-xray/uwp_net_sink.h
)

target_include_directories(xray-sock PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../x64/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../x64/include/lua>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${EXTERNAL}/spdlog/include>
    $<BUILD_INTERFACE:${EXTERNAL}/json/single_include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(xray-sock PUBLIC
    ws2_32
    "${CMAKE_CURRENT_SOURCE_DIR}/../x64/lib/lua5.4.lib"
)
target_compile_definitions(xray-sock PRIVATE XB_INSPECTOR_ENABLED WIN32_LEAN_AND_MEAN NOMINMAX)

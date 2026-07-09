cmake_minimum_required(VERSION 3.15)
project(xray-sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this to your depot checkout
add_subdirectory(../../ uwp-xray-depot)

add_executable(xray-sample main.cpp)
target_link_libraries(xray-sample PRIVATE xb-xray)
target_compile_definitions(xray-sample PRIVATE XB_INSPECTOR_ENABLED)

# UWP requires /ZW and specific subsystems — adjust for your project
# target_compile_options(xray-sample PRIVATE /ZW)
# set_target_properties(xray-sample PROPERTIES WIN32_EXECUTABLE TRUE)

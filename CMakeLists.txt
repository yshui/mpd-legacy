cmake_minimum_required(VERSION 2.8)

project(mpdr)

set(version 0)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

add_subdirectory(src)

set(sysconfdir "/etc" CACHE STRING "System config directory")

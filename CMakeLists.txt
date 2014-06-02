cmake_minimum_required(VERSION 2.8)

project(mpdr)

set(version 0)

add_subdirectory(src)

set(sysconfdir "/etc" CACHE STRING "System config directory")

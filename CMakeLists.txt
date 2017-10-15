cmake_minimum_required(VERSION 3.9)
project(xwindow)

set(CMAKE_C_FLAGS "-Wall")
set(CMAKE_C_STANDARD 11)

set(SOURCE_FILES src/xwindow.c)

add_executable(xwindow ${SOURCE_FILES})

find_package(PkgConfig)
pkg_check_modules(XCB REQUIRED xcb)

include_directories(${XCB_INCLUDE_DIRS})
target_link_libraries(xwindow ${XCB_LIBRARIES})
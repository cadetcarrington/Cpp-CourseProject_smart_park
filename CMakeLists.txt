cmake_minimum_required(VERSION 3.21)

project(SmartPark
    VERSION 0.5.0
    DESCRIPTION "Smart parking management system"
    LANGUAGES CXX
)

find_package(Qt6 6.2 REQUIRED COMPONENTS Sql Network HttpServer WebSockets)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 供 clangd、IDE 和代码分析工具使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS 默认构建原生管理端；Linux/s1 只构建服务端与终端。
option(SMARTPARK_BUILD_MACOS_ADMIN "Build the AppKit macOS administrator application" ${APPLE})

# 源码使用 UTF-8（包含中文界面字符串）。
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(src/core)
add_subdirectory(src/network)
add_subdirectory(apps/server)
add_subdirectory(apps/gate)
add_subdirectory(apps/user)

if(SMARTPARK_BUILD_MACOS_ADMIN)
    if(NOT APPLE)
        message(FATAL_ERROR "The AppKit administrator application requires macOS")
    endif()
    add_subdirectory(apps/macos)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

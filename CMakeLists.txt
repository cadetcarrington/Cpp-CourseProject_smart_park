cmake_minimum_required(VERSION 3.21)

project(SmartPark
    VERSION 0.5.0
    DESCRIPTION "Smart parking management system"
    LANGUAGES CXX
)

find_package(Qt6 6.2 REQUIRED COMPONENTS Sql Network)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 供 clangd、IDE 和代码分析工具使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SMARTPARK_BUILD_ADMIN "Build the Qt administrator application" ON)
option(SMARTPARK_BUILD_MACOS_ADMIN "Build the AppKit macOS administrator application" OFF)

# 源码使用 UTF-8（包含中文界面字符串）。MSVC 需要显式指定，
# GCC / Clang 默认按 UTF-8 处理，避免 Windows 构建出现 C4819 或乱码。
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(src/core)
add_subdirectory(src/network)
if(APPLE)
    # Qt 无关的 macOS 原生层：AppKit 毛玻璃通知横幅（车辆离场窗口通知）。
    add_subdirectory(src/platform)
endif()
add_subdirectory(apps/cli)
add_subdirectory(apps/server)
add_subdirectory(apps/gate)
add_subdirectory(apps/user)

if(SMARTPARK_BUILD_ADMIN)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    add_subdirectory(apps/admin)
endif()

if(SMARTPARK_BUILD_MACOS_ADMIN AND APPLE)
    add_subdirectory(apps/macos)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

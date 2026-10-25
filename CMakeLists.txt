cmake_minimum_required(VERSION 3.21)

project(SmartPark
    VERSION 0.6.0
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

# Qt 6.8 仅支持 macOS 12 及以上；显式声明部署目标（驱动
# -mmacosx-version-min 旗标并写入 .app 的 LSMinimumSystemVersion）。
# 用户可通过 -DCMAKE_OSX_DEPLOYMENT_TARGET=... 覆盖；FORCE 回填空缓存项，
# 否则旧构建目录里的空条目会让默认值失效。
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING
        "Minimum macOS deployment version" FORCE)
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

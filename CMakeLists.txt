cmake_minimum_required(VERSION 3.21)

project(SmartPark
    VERSION 0.4.0
    DESCRIPTION "Smart parking management system"
    LANGUAGES CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 供 clangd、IDE 和代码分析工具使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SMARTPARK_BUILD_ADMIN "Build the Qt administrator application" ON)

add_subdirectory(src/core)
add_subdirectory(apps/cli)

if(SMARTPARK_BUILD_ADMIN)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    add_subdirectory(apps/admin)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# 以下目标在对应模块开始实现时再启用。
# add_subdirectory(apps/server)
# add_subdirectory(apps/gate)

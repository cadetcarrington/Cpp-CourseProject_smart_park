cmake_minimum_required(VERSION 3.21)

project(SmartPark)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

# 供 clangd、IDE 和代码分析工具使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt 自动处理 QObject、.ui 和 .qrc 文件。
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_subdirectory(src/core)
add_subdirectory(apps/admin)

# 以下目标在对应模块开始实现时再启用。
# add_subdirectory(apps/server)
# add_subdirectory(apps/gate)
# add_subdirectory(tests)
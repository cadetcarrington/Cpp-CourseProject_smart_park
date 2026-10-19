add_executable(smartpark_core_tests
    core_model_tests.cpp
)

target_link_libraries(smartpark_core_tests
    PRIVATE
        smartpark_core
)

add_test(NAME smartpark_core_tests COMMAND smartpark_core_tests)

add_executable(smartpark_background_planning_tests background_planning_tests.cpp)
target_link_libraries(smartpark_background_planning_tests PRIVATE smartpark_core)
add_test(NAME smartpark_background_planning_tests COMMAND smartpark_background_planning_tests)
set_tests_properties(smartpark_background_planning_tests PROPERTIES TIMEOUT 90)

# Gate 断线补报「部分确认」：真实 OfflineQueue + 真实协议分帧，
# 外加进程内 mock 服务端只发半帧应答，验证失败安全。
if(TARGET smartpark_gate)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    qt_add_executable(smartpark_gate_replay_tests
        gate_replay_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/gate/OfflineQueue.cpp
        ${CMAKE_SOURCE_DIR}/apps/gate/OfflineQueue.h
    )
    target_include_directories(smartpark_gate_replay_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/apps/gate
    )
    target_compile_features(smartpark_gate_replay_tests PRIVATE cxx_std_17)
    target_link_libraries(smartpark_gate_replay_tests
        PRIVATE
            Qt6::Network
            smartpark_network
    )
    set_target_properties(smartpark_gate_replay_tests PROPERTIES AUTOMOC ON)
    add_test(NAME smartpark_gate_replay_tests
             COMMAND smartpark_gate_replay_tests)
endif()

# 车牌识别并发/进程泄漏回归：假 shell 脚本当 lprCommand，真实覆盖
# 并发上限 + 有界排队（排满立刻失败）、超时杀整棵进程树（setsid 进程组）、
# context 销毁取消（不回调、槽位恢复）、正常 JSON 解析、同步入口超时清理。
# 调度器参数在进程内只读一次，测试在 main() 开头统一设 2 / 4 / 500ms。
add_executable(smartpark_lpr_concurrency_tests lpr_concurrency_tests.cpp)
target_compile_features(smartpark_lpr_concurrency_tests PRIVATE cxx_std_17)
target_link_libraries(smartpark_lpr_concurrency_tests
    PRIVATE
        smartpark_network
        Qt6::Core
)
add_test(NAME smartpark_lpr_concurrency_tests
         COMMAND smartpark_lpr_concurrency_tests)
set_tests_properties(smartpark_lpr_concurrency_tests PROPERTIES TIMEOUT 120)

if(APPLE AND TARGET smartpark_admin_macos)
    enable_language(OBJCXX)
    # ParkingBridge 支持远程模式后会引用 RemoteDataSource，必须一起编译，
    # 否则链接期报 make_unique<RemoteDataSource> 未定义。
    # RemoteDataSource 是 QObject（ServerSession 用 Qt signals），需要 AUTOMOC。
    add_executable(smartpark_macos_map_tests
        macos_map_tests.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/ParkingMapView.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/PlatePhotoArchive.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/bridge/ParkingBridge.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/bridge/RemoteDataSource.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/bridge/RemoteDataSource.h
    )
    target_include_directories(smartpark_macos_map_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/macos)
    target_compile_features(smartpark_macos_map_tests PRIVATE cxx_std_17)
    target_compile_options(smartpark_macos_map_tests PRIVATE -fobjc-arc)
    # 测试要读仓库里的样例布局（data/garage-3tower.txt）来验证 --layout 的开库路径。
    target_compile_definitions(smartpark_macos_map_tests PRIVATE
        SMARTPARK_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
    set_target_properties(smartpark_macos_map_tests PROPERTIES AUTOMOC ON)
    # ParkingBridge 支持远程模式后经 RemoteDataSource 用到 ServerSession，
    # 因此这里也要链 smartpark_network（随之带入 Qt Network/WebSockets/HttpServer）。
    target_link_libraries(smartpark_macos_map_tests PRIVATE
        smartpark_core smartpark_network ${APPKIT_FRAMEWORK})
    add_test(NAME smartpark_macos_map_tests COMMAND smartpark_macos_map_tests)

    # 远程模式端到端：自带进程内服务端，把真实 MainWindowController 按远程方式接上去，
    # 验证快照数据 + 页面可见性 + 各页渲染。复用管理端全部源码，保证测的就是真界面。
    # 也可以指向一台真实服务端：smartpark_macos_remote_tests --remote <host> <port> <user>
    # MACOS_ADMIN_SOURCES 里的路径是相对 apps/macos 的，这里补成绝对；
    # main.mm 要剔除——它自带 main()，会和测试自己的入口冲突。
    set(_macos_app_sources "")
    foreach(_src IN LISTS MACOS_ADMIN_SOURCES)
        if(NOT _src STREQUAL "main.mm")
            list(APPEND _macos_app_sources ${CMAKE_SOURCE_DIR}/apps/macos/${_src})
        endif()
    endforeach()
    add_executable(smartpark_macos_remote_tests
        macos_remote_tests.mm
        ${_macos_app_sources}
    )
    target_include_directories(smartpark_macos_remote_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/macos)
    target_compile_features(smartpark_macos_remote_tests PRIVATE cxx_std_17)
    target_compile_options(smartpark_macos_remote_tests PRIVATE -fobjc-arc)
    set_target_properties(smartpark_macos_remote_tests PROPERTIES AUTOMOC ON)
    target_compile_definitions(smartpark_macos_remote_tests PRIVATE
        SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py")
    find_library(SECURITY_FRAMEWORK_FOR_TESTS Security REQUIRED)
    target_link_libraries(smartpark_macos_remote_tests PRIVATE
        smartpark_core smartpark_network
        ${APPKIT_FRAMEWORK} ${SECURITY_FRAMEWORK_FOR_TESTS})
    add_test(NAME smartpark_macos_remote_tests COMMAND smartpark_macos_remote_tests)
endif()

# REST + TCP 端到端：起一个真实服务端进程，把所有业务链路走一遍。
# 比单元测试慢（约 30 秒），带 e2e 标签，`ctest -LE e2e` 可跳过。
if(TARGET smartpark_server)
    find_program(SMARTPARK_BASH bash)
    if(SMARTPARK_BASH)
        add_test(NAME smartpark_rest_e2e
                 COMMAND ${SMARTPARK_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/e2e_rest.sh
                         --server $<TARGET_FILE:smartpark_server>
                         --web-root ${CMAKE_SOURCE_DIR}/apps/webclient
                         --layout ${CMAKE_SOURCE_DIR}/data/garage-6f.txt)
        set_tests_properties(smartpark_rest_e2e PROPERTIES
            LABELS e2e
            RUN_SERIAL TRUE
            TIMEOUT 240)
    endif()
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME smartpark_lpr_script_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_recognize_plate.py)
    add_test(NAME smartpark_lpr_resident_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_lpr_resident.py)
    # 无 numpy/opencv 的解释器会自动跳过视觉用例（exit 0）。
    add_test(NAME smartpark_plate_geometry_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_plate_geometry.py)
    add_test(NAME smartpark_plate_dataset_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_plate_datasets.py)
    # 底色判定只需要 numpy；缺 numpy 时同样自动跳过。
    add_test(NAME smartpark_plate_color_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_plate_color.py)
endif()

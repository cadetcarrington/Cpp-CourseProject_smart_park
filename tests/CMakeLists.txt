add_executable(smartpark_core_tests
    core_model_tests.cpp
)

target_link_libraries(smartpark_core_tests
    PRIVATE
        smartpark_core
)

add_test(NAME smartpark_core_tests COMMAND smartpark_core_tests)

# Gate 断线补报「部分确认」：真实 OfflineQueue + 真实协议分帧，
# 外加进程内 mock 服务端只发半帧应答，验证失败安全。
if(TARGET smartpark_gate)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    qt_add_executable(smartpark_gate_replay_tests
        gate_replay_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/gate/BarrierGate.cpp
        ${CMAKE_SOURCE_DIR}/apps/gate/BarrierGate.h
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

if(APPLE AND TARGET smartpark_admin_macos)
    enable_language(OBJCXX)
    add_executable(smartpark_macos_map_tests
        macos_map_tests.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/ParkingMapView.mm
        ${CMAKE_SOURCE_DIR}/apps/macos/bridge/ParkingBridge.mm
    )
    target_include_directories(smartpark_macos_map_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/apps/macos)
    target_compile_features(smartpark_macos_map_tests PRIVATE cxx_std_17)
    target_compile_options(smartpark_macos_map_tests PRIVATE -fobjc-arc)
    target_link_libraries(smartpark_macos_map_tests PRIVATE
        smartpark_core ${APPKIT_FRAMEWORK})
    add_test(NAME smartpark_macos_map_tests COMMAND smartpark_macos_map_tests)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME smartpark_lpr_script_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_recognize_plate.py)
    # 无 numpy/opencv 的解释器会自动跳过视觉用例（exit 0）。
    add_test(NAME smartpark_plate_geometry_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_plate_geometry.py)
    add_test(NAME smartpark_plate_dataset_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_plate_datasets.py)
endif()

if(TARGET smartpark_admin)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
    qt_add_executable(smartpark_admin_tests
        admin_main_window_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/PlateReviewDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/LoginDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/RegisterDialog.cpp
    )
    target_include_directories(smartpark_admin_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/apps/admin
    )
    target_compile_definitions(smartpark_admin_tests PRIVATE
        SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py"
        SMARTPARK_PLATE_EXAMPLES_PATH="${CMAKE_SOURCE_DIR}/examples/plates")
    target_link_libraries(smartpark_admin_tests
        PRIVATE
            Qt6::Test
            Qt6::Widgets
            smartpark_core
            smartpark_network
    )
    set_target_properties(smartpark_admin_tests PROPERTIES AUTOMOC ON)
    add_test(NAME smartpark_admin_tests COMMAND smartpark_admin_tests)
    set_tests_properties(smartpark_admin_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # 远程模式集成测试：进程内 TCP 服务端 + ServerSession + 远程 MainWindow。
    qt_add_executable(smartpark_admin_remote_tests
        admin_remote_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/PlateReviewDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/LoginDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/RegisterDialog.cpp
    )
    target_include_directories(smartpark_admin_remote_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/apps/admin
    )
    target_compile_definitions(smartpark_admin_remote_tests PRIVATE
        SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py"
        SMARTPARK_PLATE_EXAMPLES_PATH="${CMAKE_SOURCE_DIR}/examples/plates")
    target_link_libraries(smartpark_admin_remote_tests
        PRIVATE
            Qt6::Test
            Qt6::Widgets
            smartpark_core
            smartpark_network
    )
    set_target_properties(smartpark_admin_remote_tests PROPERTIES AUTOMOC ON)
    add_test(NAME smartpark_admin_remote_tests
        COMMAND smartpark_admin_remote_tests)
    set_tests_properties(smartpark_admin_remote_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SMARTPARK_NO_MENU_BAR=1"
    )
endif()

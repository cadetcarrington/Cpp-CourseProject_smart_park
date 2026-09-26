add_executable(smartpark_core_tests
    core_model_tests.cpp
)

target_link_libraries(smartpark_core_tests
    PRIVATE
        smartpark_core
)

add_test(NAME smartpark_core_tests COMMAND smartpark_core_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME smartpark_lpr_script_tests
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_recognize_plate.py)
endif()

if(TARGET smartpark_admin)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
    qt_add_executable(smartpark_admin_tests
        admin_main_window_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/PlateReviewDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/LoginDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/RegisterDialog.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/UserStore.cpp
    )
    target_include_directories(smartpark_admin_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/apps/admin
    )
    target_compile_definitions(smartpark_admin_tests PRIVATE
        SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py")
    target_link_libraries(smartpark_admin_tests
        PRIVATE
            Qt6::Test
            Qt6::Widgets
            smartpark_core
    )
    set_target_properties(smartpark_admin_tests PROPERTIES AUTOMOC ON)
    add_test(NAME smartpark_admin_tests COMMAND smartpark_admin_tests)
    set_tests_properties(smartpark_admin_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

add_executable(smartpark_core_tests
    core_model_tests.cpp
)

target_link_libraries(smartpark_core_tests
    PRIVATE
        smartpark_core
)

add_test(NAME smartpark_core_tests COMMAND smartpark_core_tests)

if(TARGET smartpark_admin)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
    qt_add_executable(smartpark_admin_tests
        admin_main_window_tests.cpp
        ${CMAKE_SOURCE_DIR}/apps/admin/MainWindow.cpp
    )
    target_include_directories(smartpark_admin_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/apps/admin
    )
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

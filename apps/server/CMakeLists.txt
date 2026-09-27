qt_add_executable(smartpark_server
    main.cpp
)

target_link_libraries(smartpark_server
    PRIVATE
        smartpark_network
        smartpark_core
)

target_compile_features(smartpark_server PRIVATE cxx_std_17)
set_target_properties(smartpark_server PROPERTIES AUTOMOC ON)

if(BUILD_TESTING)
    add_test(NAME smartpark_server_selftest
             COMMAND smartpark_server --selftest)
endif()

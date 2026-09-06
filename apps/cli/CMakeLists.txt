add_executable(smartpark_cli main.cpp)

target_link_libraries(smartpark_cli PRIVATE smartpark_core)

if(BUILD_TESTING)
    add_test(NAME smartpark_cli_demo
             COMMAND smartpark_cli --db ${CMAKE_CURRENT_BINARY_DIR}/smartpark_cli_demo.db --reset)
endif()

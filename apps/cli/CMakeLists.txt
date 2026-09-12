add_executable(smartpark_cli main.cpp)

target_link_libraries(smartpark_cli PRIVATE smartpark_core)

if(BUILD_TESTING)
    add_test(NAME smartpark_cli_demo
             COMMAND smartpark_cli --db ${CMAKE_CURRENT_BINARY_DIR}/smartpark_cli_demo.db --reset)
    add_test(NAME smartpark_cli_booking
             COMMAND ${CMAKE_COMMAND}
                     -DSMARTPARK_CLI=$<TARGET_FILE:smartpark_cli>
                     -DDB_PATH=${CMAKE_CURRENT_BINARY_DIR}/smartpark_cli_booking.db
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/booking_command_test.cmake)
endif()

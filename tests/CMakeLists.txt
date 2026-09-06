add_executable(smartpark_core_tests
    core_model_tests.cpp
)

target_link_libraries(smartpark_core_tests
    PRIVATE
        smartpark_core
)

add_test(NAME smartpark_core_tests COMMAND smartpark_core_tests)

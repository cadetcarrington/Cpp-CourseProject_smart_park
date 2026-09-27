qt_add_executable(smartpark_user
    main.cpp
)

target_link_libraries(smartpark_user
    PRIVATE
        smartpark_network
        smartpark_core
)

target_compile_features(smartpark_user PRIVATE cxx_std_17)
set_target_properties(smartpark_user PROPERTIES AUTOMOC ON)

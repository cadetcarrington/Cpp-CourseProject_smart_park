qt_add_executable(smartpark_admin
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

set_target_properties(smartpark_admin PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

target_link_libraries(smartpark_admin
    PRIVATE
        Qt6::Widgets
        smartpark_core
)

target_compile_features(smartpark_admin PRIVATE cxx_std_17)

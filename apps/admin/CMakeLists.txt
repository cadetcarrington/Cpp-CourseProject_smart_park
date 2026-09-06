qt_add_executable(smartpark_admin
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(smartpark_admin
    PRIVATE
        Qt6::Widgets
        smartpark_core
)

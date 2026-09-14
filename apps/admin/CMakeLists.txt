if(WIN32)
    qt_add_executable(smartpark_admin WIN32
        main.cpp
        MainWindow.h
        MainWindow.cpp
    )
elseif(APPLE)
    qt_add_executable(smartpark_admin MACOSX_BUNDLE
        main.cpp
        MainWindow.h
        MainWindow.cpp
    )
else()
    qt_add_executable(smartpark_admin
        main.cpp
        MainWindow.h
        MainWindow.cpp
    )
endif()

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

if(APPLE)
    set_target_properties(smartpark_admin PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "SmartPark Admin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.smartpark.admin"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

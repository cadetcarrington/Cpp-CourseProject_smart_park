if(WIN32)
    qt_add_executable(smartpark_admin WIN32
        main.cpp
        MainWindow.h
        MainWindow.cpp
        LoginDialog.h
        LoginDialog.cpp
    )
elseif(APPLE)
    qt_add_executable(smartpark_admin MACOSX_BUNDLE
        main.cpp
        MainWindow.h
        MainWindow.cpp
        LoginDialog.h
        LoginDialog.cpp
    )
else()
    qt_add_executable(smartpark_admin
        main.cpp
        MainWindow.h
        MainWindow.cpp
        LoginDialog.h
        LoginDialog.cpp
    )
endif()

set_target_properties(smartpark_admin PROPERTIES
    AUTOMOC ON
    # Admin UI is built entirely in C++; there are no .ui files to process.
    # Disabling AUTOUIC avoids invoking Qt's uic helper on macOS packages
    # whose bundled helper may require an unavailable CPU feature.
    AUTOUIC OFF
    # The target has no Qt resource collection either.
    AUTORCC OFF
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

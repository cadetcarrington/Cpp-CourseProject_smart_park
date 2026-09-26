set(ADMIN_SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    PlateReviewDialog.h
    PlateReviewDialog.cpp
    LoginDialog.h
    LoginDialog.cpp
    RegisterDialog.h
    RegisterDialog.cpp
    UserStore.h
    UserStore.cpp
    Theme.h
    NativeEffects.h
    MacSystemBridge.h
)
if(APPLE)
    # ObjC++ 桥接层：NSVisualEffectView 毛玻璃 + 菜单栏余位/通知中心/
    # 中文语音/PDF 导出/NSURLSession（MacSystemBridge）。
    enable_language(OBJCXX)
    list(APPEND ADMIN_SOURCES NativeEffects.mm MacSystemBridge.mm)
endif()

if(WIN32)
    qt_add_executable(smartpark_admin WIN32 ${ADMIN_SOURCES})
elseif(APPLE)
    qt_add_executable(smartpark_admin MACOSX_BUNDLE ${ADMIN_SOURCES})
else()
    qt_add_executable(smartpark_admin ${ADMIN_SOURCES})
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
target_compile_definitions(smartpark_admin PRIVATE
    SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py")

if(APPLE)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(USERNOTIFICATIONS_FRAMEWORK UserNotifications REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(PDFKIT_FRAMEWORK PDFKit REQUIRED)
    target_link_libraries(smartpark_admin PRIVATE ${APPKIT_FRAMEWORK}
        ${USERNOTIFICATIONS_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
        ${PDFKIT_FRAMEWORK})
    target_compile_definitions(smartpark_admin PRIVATE
        SMARTPARK_HAS_NATIVE_VIBRANCY SMARTPARK_MACOS_NATIVE)
    set_target_properties(smartpark_admin PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "SmartPark Admin"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.smartpark.admin"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()

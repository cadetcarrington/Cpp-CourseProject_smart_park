# AppKit 版 macOS 管理端（POC）。
# 复用 C++17 core（算法/SQLite/Service），仅 UI 层用原生 AppKit 重写，
# 与 Qt 版（apps/admin）并行，成熟后再删除 Qt 版。

enable_language(OBJCXX)

set(MACOS_ADMIN_SOURCES
    main.mm
    AppDelegate.h
    AppDelegate.mm
    MainWindowController.h
    MainWindowController.mm
    LoginViewController.h
    LoginViewController.mm
    RegisterViewController.h
    RegisterViewController.mm
    SidebarViewController.h
    SidebarViewController.mm
    DashboardViewController.h
    DashboardViewController.mm
    ParkingMapView.h
    ParkingMapView.mm
    ParkingMapViewController.h
    ParkingMapViewController.mm
    DonutChartView.h
    DonutChartView.mm
    BarChartView.h
    BarChartView.mm
    LineChartView.h
    LineChartView.mm
    TableView.h
    TableView.mm
    TextUtil.h
    OperationsViewController.h
    OperationsViewController.mm
    OccupancyViewController.h
    OccupancyViewController.mm
    BookingViewController.h
    BookingViewController.mm
    RecordsViewController.h
    RecordsViewController.mm
    SettingsViewController.h
    SettingsViewController.mm
    bridge/ParkingBridge.h
    bridge/ParkingBridge.mm
)

add_executable(smartpark_admin_macos MACOSX_BUNDLE ${MACOS_ADMIN_SOURCES})

target_compile_features(smartpark_admin_macos PRIVATE cxx_std_17)

# 让 #import "bridge/ParkingBridge.h"、#import "AppDelegate.h" 等可解析。
target_include_directories(smartpark_admin_macos PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# core/network 已 PUBLIC 链接 Qt6::Sql / Qt6::Network，无需 Widgets。
target_link_libraries(smartpark_admin_macos
    PRIVATE
        smartpark_core
        smartpark_network
)

find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
target_link_libraries(smartpark_admin_macos PRIVATE ${APPKIT_FRAMEWORK})

# 全部 ObjC++ 源码启用 ARC，简化内存管理。
target_compile_options(smartpark_admin_macos PRIVATE -fobjc-arc)

target_compile_definitions(smartpark_admin_macos PRIVATE
    SMARTPARK_LPR_SCRIPT_PATH="${CMAKE_SOURCE_DIR}/scripts/recognize_plate.py"
    SMARTPARK_PLATE_EXAMPLES_PATH="${CMAKE_SOURCE_DIR}/examples/plates")

set_target_properties(smartpark_admin_macos PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "SmartPark Admin"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.smartpark.admin.macos"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

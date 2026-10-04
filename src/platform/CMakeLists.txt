enable_language(OBJCXX)

# Qt 无关的 macOS 原生层：AppKit 毛玻璃通知横幅（车辆离场窗口通知）。
add_library(smartpark_platform STATIC MacNotifications.mm)
set_source_files_properties(MacNotifications.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_include_directories(smartpark_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(smartpark_platform PUBLIC cxx_std_17)
find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
target_link_libraries(smartpark_platform PUBLIC ${APPKIT_FRAMEWORK})

if(BUILD_TESTING)
    add_executable(smartpark_platform_tests platform_tests.mm)
    set_source_files_properties(platform_tests.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(smartpark_platform_tests PRIVATE smartpark_platform)
    add_test(NAME smartpark_platform_tests COMMAND smartpark_platform_tests)
endif()

add_library(smartpark_core INTERFACE)

target_compile_features(smartpark_core INTERFACE cxx_std_17)

target_include_directories(smartpark_core
    INTERFACE
        ${PROJECT_SOURCE_DIR}/src
)

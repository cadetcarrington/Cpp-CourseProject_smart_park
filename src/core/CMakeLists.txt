add_library(smartpark_core STATIC
    model/ParkingSpot.cpp
    model/ParkingSpot.h
    model/Vehicle.cpp
    model/Vehicle.h
)

target_compile_features(smartpark_core PUBLIC cxx_std_17)

target_include_directories(smartpark_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

add_library(smartpark_core STATIC
    model/Geometry.h
    model/ParkingLayout.cpp
    model/ParkingLayout.h
    model/ParkingRecord.cpp
    model/ParkingRecord.h
    model/ParkingSpot.cpp
    model/ParkingSpot.h
    model/Vehicle.cpp
    model/Vehicle.h
    service/GridPlanner.cpp
    service/GridPlanner.h
    service/ParkingService.cpp
    service/ParkingService.h
)

target_compile_features(smartpark_core PUBLIC cxx_std_17)

target_include_directories(smartpark_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

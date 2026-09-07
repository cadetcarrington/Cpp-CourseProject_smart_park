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
    util/TimeUtil.h
    service/Billing.cpp
    service/Billing.h
    service/GridPlanner.cpp
    service/GridPlanner.h
    service/ParkingService.cpp
    service/ParkingService.h
    service/SpotAllocator.cpp
    service/SpotAllocator.h
    persistence/DatabaseManager.cpp
    persistence/DatabaseManager.h
    persistence/Persistence.cpp
    persistence/Persistence.h
    persistence/ParkingRepository.cpp
    persistence/ParkingRepository.h
)

target_compile_features(smartpark_core PUBLIC cxx_std_17)

target_include_directories(smartpark_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(smartpark_core PUBLIC Qt6::Sql)

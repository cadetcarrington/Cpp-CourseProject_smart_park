add_library(smartpark_core STATIC
    model/Geometry.h
    model/Booking.cpp
    model/Booking.h
    model/ParkingLayout.cpp
    model/ParkingLayout.h
    model/ParkingRecord.cpp
    model/ParkingRecord.h
    model/ParkingSpot.cpp
    model/ParkingSpot.h
    model/Reservation.cpp
    model/Reservation.h
    model/Vehicle.cpp
    model/Vehicle.h
    util/TimeUtil.h
    service/AuditLogService.cpp
    service/AuditLogService.h
    service/UserStore.cpp
    service/UserStore.h
    service/Billing.cpp
    service/Billing.h
    service/DemoDirector.cpp
    service/DemoDirector.h
    service/AnalyticsEngine.cpp
    service/AnalyticsEngine.h
    service/RemoteAnalystClient.cpp
    service/RemoteAnalystClient.h
    service/FakePaymentGateway.cpp
    service/FakePaymentGateway.h
    service/GridPlanner.cpp
    service/GridPlanner.h
    service/BackgroundEntryPlanner.cpp
    service/BackgroundEntryPlanner.h
    service/ParkingService.cpp
    service/ParkingService.h
    service/ParkingInsightEngine.cpp
    service/ParkingInsightEngine.h
    service/ReservationService.cpp
    service/ReservationService.h
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

# AppleClang 通常未随附 OpenMP；未找到时保留后台执行和串行搜索。
option(SMARTPARK_ENABLE_OPENMP "Parallelize independent gate route searches" ON)
if(SMARTPARK_ENABLE_OPENMP)
    find_package(OpenMP QUIET COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(smartpark_core PUBLIC OpenMP::OpenMP_CXX)
        message(STATUS "SmartPark route planning: OpenMP enabled")
    else()
        message(STATUS "SmartPark route planning: serial fallback (OpenMP not found)")
    endif()
endif()

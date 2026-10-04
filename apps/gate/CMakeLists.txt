qt_add_executable(smartpark_gate
    main.cpp
    BarrierGate.cpp
    BarrierGate.h
    GateClock.cpp
    GateClock.h
    QtGateClock.cpp
    QtGateClock.h
    OfflineQueue.cpp
    OfflineQueue.h
    GateSelftest.cpp
    GateSelftest.h
)

target_link_libraries(smartpark_gate
    PRIVATE
        smartpark_network
        smartpark_core
)

target_compile_features(smartpark_gate PRIVATE cxx_std_17)
set_target_properties(smartpark_gate PROPERTIES AUTOMOC ON)

if(BUILD_TESTING)
    add_test(NAME smartpark_gate_selftest
             COMMAND smartpark_gate --selftest)
endif()

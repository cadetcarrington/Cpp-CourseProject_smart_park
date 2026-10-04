find_package(Qt6 6.2 REQUIRED COMPONENTS Network)

add_library(smartpark_network STATIC
    ServerSession.cpp
    ServerSession.h
    Protocol.cpp
    Protocol.h
    SmartParkTcpServer.cpp
    SmartParkTcpServer.h
    TcpClient.cpp
    TcpClient.h
)

target_link_libraries(smartpark_network
    PUBLIC
        Qt6::Network
        smartpark_core
)

target_compile_features(smartpark_network PUBLIC cxx_std_17)
set_target_properties(smartpark_network PROPERTIES AUTOMOC ON)

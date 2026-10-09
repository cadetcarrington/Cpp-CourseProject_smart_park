find_package(Qt6 6.2 REQUIRED COMPONENTS Network HttpServer WebSockets)

add_library(smartpark_network STATIC
    EventHub.h
    ServerSession.cpp
    ServerSession.h
    PlateRecognition.cpp
    PlateRecognition.h
    Protocol.cpp
    Protocol.h
    RestGateway.cpp
    RestGateway.h
    SmartParkTcpServer.cpp
    SmartParkTcpServer.h
    TcpClient.cpp
    TcpClient.h
)

target_link_libraries(smartpark_network
    PUBLIC
        Qt6::Network
        Qt6::HttpServer
        Qt6::WebSockets
        smartpark_core
)

# Nayuki QR-Code-generator（MIT，third_party/QR-Code-generator）：
# REST 网关的 /api/v1/qr 与启动横幅二维码。
target_sources(smartpark_network PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/QR-Code-generator/cpp/qrcodegen.cpp
)
target_include_directories(smartpark_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/QR-Code-generator/cpp
)

target_compile_features(smartpark_network PUBLIC cxx_std_17)
set_target_properties(smartpark_network PROPERTIES AUTOMOC ON)

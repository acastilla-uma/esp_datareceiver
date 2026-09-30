cmake_minimum_required(VERSION 3.10)
project(ESP32DataReceiver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
find_package(Threads REQUIRED)

# Programa principal
add_executable(esp32_receiver
    main.cpp
    gnss_receiver.cpp
    gps_matcher.cpp
    ntrip_client.cpp
    serial_format.cpp
    udp_gateway.cpp
)
target_link_libraries(esp32_receiver PRIVATE Threads::Threads)

add_executable(gps_matcher_test tests/gps_matcher_test.cpp gps_matcher.cpp)
target_include_directories(gps_matcher_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gps_matcher_test COMMAND gps_matcher_test)

add_executable(serial_format_test tests/serial_format_test.cpp serial_format.cpp)
target_include_directories(serial_format_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME serial_format_test COMMAND serial_format_test)

add_executable(gnss_receiver_test tests/gnss_receiver_test.cpp gnss_receiver.cpp)
target_include_directories(gnss_receiver_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gnss_receiver_test COMMAND gnss_receiver_test)

add_executable(ntrip_client_test ../tests/ntrip_client_test.cpp ntrip_client.cpp)
target_include_directories(ntrip_client_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ntrip_client_test PRIVATE Threads::Threads)
add_test(NAME ntrip_client_test COMMAND ntrip_client_test)

add_executable(udp_gateway_test tests/udp_gateway_test.cpp udp_gateway.cpp)
target_include_directories(udp_gateway_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME udp_gateway_test COMMAND udp_gateway_test)

# Opciones de compilación
if(UNIX AND NOT APPLE)
    target_compile_options(esp32_receiver PRIVATE -Wall -Wextra -O2)
    target_compile_options(gps_matcher_test PRIVATE -Wall -Wextra -O2)
    target_compile_options(serial_format_test PRIVATE -Wall -Wextra -O2)
    target_compile_options(gnss_receiver_test PRIVATE -Wall -Wextra -O2)
    target_compile_options(ntrip_client_test PRIVATE -Wall -Wextra -O2)
    target_compile_options(udp_gateway_test PRIVATE -Wall -Wextra -O2)
endif()

# Instalación
install(TARGETS esp32_receiver DESTINATION bin)

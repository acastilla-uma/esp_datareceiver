cmake_minimum_required(VERSION 3.10)
project(ESP32DataReceiver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Programa principal
add_executable(esp32_receiver main.cpp)

# Opciones de compilación
if(UNIX AND NOT APPLE)
    target_compile_options(esp32_receiver PRIVATE -Wall -Wextra -O2)
endif()

# Instalación
install(TARGETS esp32_receiver DESTINATION bin)

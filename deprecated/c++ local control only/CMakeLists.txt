cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.5.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(blink C CXX ASM)
set(PICO_BOARD pico_w)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(blink blink.cpp)

# pull in common dependencies
target_link_libraries(blink pico_stdlib pico_cyw43_arch_none)

# Disable UART, Enable USB
pico_enable_stdio_uart(blink 0)
pico_enable_stdio_usb(blink 1)

# create bin/dis/hex/uf2 files
pico_add_extra_outputs(blink)

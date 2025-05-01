cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_examples C CXX ASM)
set(PICO_BOARD pico_w)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_EXAMPLES_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

# main controls three sets of blinds
add_executable(main main.cpp blinds.cpp)

# Enable USB serial for optional debug
pico_enable_stdio_uart(main 0)
pico_enable_stdio_usb(main 1)

# pull in common dependencies
target_link_libraries(main pico_stdlib pico_cyw43_arch_none hardware_rtc)

# create map/bin/hex file etc.
pico_add_extra_outputs(main)

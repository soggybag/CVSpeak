# CVSpeak firmware for the Raspberry Pi Pico 2 (RP2350, Arm cores).
#   make firmware        (from the repository root; downloads nothing)
# or by hand:
#   cmake -S firmware -B build/firmware -DPICO_SDK_PATH=sdk/pico-sdk
#   cmake --build build/firmware
# Output: build/firmware/cvspeak.uf2. Hold BOOTSEL, plug in, drag it onto RP2350.

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../sdk/pico-sdk)
endif()

include(pico_sdk_import.cmake)

project(cvspeak C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(DSP ${CMAKE_CURRENT_LIST_DIR}/../dsp)
file(GLOB DSP_SOURCES ${DSP}/src/*.cpp)

add_executable(cvspeak
    src/main.cpp
    src/audio.cpp
    src/controls.cpp
    src/es8388.cpp
    src/oled.cpp
    ${DSP_SOURCES}
)

target_include_directories(cvspeak PRIVATE src ${DSP}/include)
target_compile_options(cvspeak PRIVATE -O2 -Wall -Wextra -Wdouble-promotion -Wno-unused-parameter)

pico_generate_pio_header(cvspeak ${CMAKE_CURRENT_LIST_DIR}/src/i2s.pio)

target_link_libraries(cvspeak
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_clocks
    hardware_i2c
    hardware_pio
    hardware_spi
)

option(CVSPEAK_BREADBOARD "Bare Pico 2: no analog stages, buttons on GP15/20/22" OFF)
if(CVSPEAK_BREADBOARD)
    target_compile_definitions(cvspeak PRIVATE CVSPEAK_BREADBOARD=1)
endif()

option(CVSPEAK_SSD1306 "SSD1306 (0.96in) OLED instead of the SH1106 (1.3in)" OFF)
if(CVSPEAK_SSD1306)
    target_compile_definitions(cvspeak PRIVATE CVSPEAK_SSD1306=1)
endif()

# Debug text over USB serial (printf); the UART pins are used for other things.
pico_enable_stdio_usb(cvspeak 1)
pico_enable_stdio_uart(cvspeak 0)

pico_set_program_name(cvspeak "CVSpeak")
pico_add_extra_outputs(cvspeak)

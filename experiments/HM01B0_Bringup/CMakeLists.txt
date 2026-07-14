cmake_minimum_required(VERSION 3.13)

# Uses the Pico SDK from the PICO_SDK_PATH environment variable.
if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Point it at your pico-sdk checkout.")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(hm01b0_bringup C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(hm01b0_bringup
    main.c
)

pico_generate_pio_header(
    hm01b0_bringup
    ${CMAKE_CURRENT_LIST_DIR}/hm01b0_parallel.pio
)

target_include_directories(hm01b0_bringup PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(hm01b0_bringup
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_i2c
    hardware_pio
    hardware_pwm
)

# USB serial is convenient for the first bring-up.
pico_enable_stdio_usb(hm01b0_bringup 1)
pico_enable_stdio_uart(hm01b0_bringup 0)

pico_add_extra_outputs(hm01b0_bringup)

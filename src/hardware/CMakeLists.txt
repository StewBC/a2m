# Stefan Wessels, November 2025
# Builds an Apple 2 emulator hardware (the hardware)
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(hardware STATIC
    6502_core.c
    6502.c
    apple2.c
    diskii.c
    frankdisp.c
    image.c
    langcard.c
    roms.c
    slot.c
    smrtprt.c
)

target_include_directories(hardware PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(hardware PUBLIC c_std_99)

target_precompile_headers(hardware PUBLIC "hardware_lib.h")

target_link_libraries(hardware PUBLIC
    utils
)
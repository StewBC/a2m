# Stefan Wessels, November 2025
# Builds an Apple 2 emulator text mode UI (when it gets built - right now this is a stub)
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(utxt STATIC
    utxt_view.c
)

target_include_directories(utxt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../debug
)

target_compile_features(utxt PUBLIC c_std_99)

target_precompile_headers(utxt PUBLIC "utxt_lib.h")

target_link_libraries(utxt PUBLIC
    asm
    hardware
    runtime
)

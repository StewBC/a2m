# Stefan Wessels, November 2025
# Builds an Apple 2 emulator common Library
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(common STATIC
    config.c
    memdst.c
    txt_row.c
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(common PUBLIC c_std_99)

target_precompile_headers(common PUBLIC "common.h")

target_link_libraries(common PUBLIC
    utils
)

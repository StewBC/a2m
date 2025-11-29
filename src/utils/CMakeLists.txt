# Stefan Wessels, November 2025
# Builds a utilities library, utils, for use in an Apple ][+ and //e Enhanced emulator with assembler
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(utils STATIC
    breakprs.c
    dbgopcds.c
    dynarray.c
    errorlog.c
    inifiles.c
    trace.c
    util.c
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}                 # Self
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/      # common.h
)

target_compile_features(utils PUBLIC c_std_99)

target_precompile_headers(utils PUBLIC "utils_lib.h")

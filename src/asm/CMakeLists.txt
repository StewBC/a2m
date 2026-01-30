# Stefan Wessels, November 2025
# Builds a 6502 assembler Library
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(asm STATIC
    asm_err.c
    tokens.c
    symbols.c
    scope.c
    segment.c
    opcds.c
    incl_fls.c
    asm6502.c
    expr.c
    gperf.c
    parse.c
    emit.c
)

target_include_directories(asm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(asm PUBLIC c_std_99)

target_precompile_headers(asm PUBLIC "asm_lib.h")

target_link_libraries(asm
    PUBLIC
        utils
)

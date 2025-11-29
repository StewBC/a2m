# Stefan Wessels, November 2025
# Builds Apple 2 emulator UIs (Nuklear and text so far) 
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

# UI implementations
add_subdirectory(nuklear)   # unk
add_subdirectory(text)      # utxt

add_library(ui STATIC
    ui.c
)

target_include_directories(ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ui PUBLIC c_std_99)

target_precompile_headers(ui PUBLIC "ui_lib.h")

target_link_libraries(ui
    PUBLIC
        runtime
        unk
        utxt
)

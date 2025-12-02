# Stefan Wessels, November 2025
# Builds an Apple 2 emulator runtime (hardware <-runtime-> ui)
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(runtime STATIC
    rt_bp.c
    rt.c
    rt_sym.c
)

target_include_directories(runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../debug
)

target_compile_features(runtime PUBLIC c_std_99)

target_precompile_headers(runtime PUBLIC "rt_lib.h")

target_link_libraries(runtime PUBLIC
    hardware
    # SQW It's not ideal that runtime links to SDL but is needed for performance ounters and such
    SDL2::SDL2
    SDL2::SDL2main
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
)
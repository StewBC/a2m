# Stefan Wessels, November 2025
# Builds an Apple 2 emulator UI using Nuklear immediate mode gui
# This is free and unencumbered software released into the public domain.

cmake_minimum_required(VERSION 3.16)

add_library(unk STATIC
    unk_joy.c
    unk_layout.c
    unk_leds.c
    unk_apl2.c
    unk_audio.c
    unk_cpu.c
    unk_dlg.c
    unk_dasm.c
    unk_help.c
    unk_htext.c
    unk_mem.c
    unk_misc.c
    unk_view.c
)

target_include_directories(unk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../debug
)

target_compile_features(unk PUBLIC c_std_99)

target_precompile_headers(unk PUBLIC "unk_lib.h")

target_link_libraries(unk PUBLIC
    asm
    common
    hardware
    runtime
    SDL2::SDL2
    SDL2::SDL2main
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<$<PLATFORM_ID:Linux>:m>
)

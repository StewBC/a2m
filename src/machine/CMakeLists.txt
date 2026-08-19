add_library(machine STATIC
    apple2.c
    apple2_snapshot.c
    ay38910.c
    cpu65.c
    diskii.c
    diskii_rom.c
    hostfs.c
    image.c
    mboard.c
    rom_data.c
    smartport_rom.c
    smrtprt.c
    softswitch.c
    via6522.c
    video.c
    keyboard.c
)

target_include_directories(machine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(machine PUBLIC c_std_99)

target_link_libraries(machine
    PUBLIC
        util
)

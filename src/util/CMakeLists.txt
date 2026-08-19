# src/util/CMakeLists.txt

add_library(util STATIC
    apple2_file.c
    apple_type_script.c
    audio_buffer.c
    config.c
    cond.c
    dynarray.c
    message_queue.c
    mutex.c
    paste_parser.c
    thread.c
    util.c
    util_file.c
)

# audio_buffer.c uses C11 _Atomic for lock-free SPSC; the rest of util stays C99.
set_source_files_properties(audio_buffer.c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

target_include_directories(util PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(util PUBLIC c_std_99)

target_link_libraries(util
    PRIVATE
        SDL2::SDL2
        a2m_inih
        a2m_stb_ds
)

# src/util/CMakeLists.txt

if(WIN32)
    set(FS_WATCH_PLATFORM_SOURCE fs_watch_windows.c)
elseif(APPLE)
    set(FS_WATCH_PLATFORM_SOURCE fs_watch_macos.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(FS_WATCH_PLATFORM_SOURCE fs_watch_linux.c)
else()
    set(FS_WATCH_PLATFORM_SOURCE fs_watch_unsupported.c)
endif()

add_library(util STATIC
    apple2_file.c
    apple_type_script.c
    audio_buffer.c
    config.c
    cond.c
    dynarray.c
    fs_watch.c
    ${FS_WATCH_PLATFORM_SOURCE}
    message_queue.c
    mutex.c
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

if(APPLE)
    find_library(CORESERVICES_FRAMEWORK CoreServices REQUIRED)
    target_link_libraries(util PRIVATE ${CORESERVICES_FRAMEWORK})
endif()

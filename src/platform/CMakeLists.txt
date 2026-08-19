# src/platform/CMakeLists.txt

add_library(platform STATIC
    platform.c
    platform_audio.c
    platform_fs.c
    platform_socket.c
)

target_include_directories(platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(platform PUBLIC c_std_99)

target_link_libraries(platform
    PUBLIC
        SDL2::SDL2
    PRIVATE
        util
)

if(WIN32)
    target_link_libraries(platform PRIVATE ws2_32)
endif()

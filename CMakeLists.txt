cmake_minimum_required(VERSION 3.16)

# Set project name
project(apple2 LANGUAGES C)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find SDL2, and SDL2_mixer
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)

# Add the executable
add_executable(${PROJECT_NAME}
    src/6502.c
    src/apple2.c
    src/breakpnt.c
    src/dbgopcds.c
    src/viewapl2.c
    src/dynarray.c
    src/main.c
    src/ramcard.c
    src/roms.c
    src/smrtprt.c
    src/util.c
    src/viewcpu.c
    src/viewdbg.c
    src/viewdlg.c
    src/viewmem.c
    src/viewmisc.c
    src/viewport.c
)

# Link SDL2, SDL2main, and SDL2_mixer libraries
target_link_libraries(${PROJECT_NAME}
    SDL2::SDL2
    SDL2::SDL2main
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    $<$<PLATFORM_ID:Linux>:m>
)

# Use this to compile only viewdbg.c with -Wno-format-security on Linux
if(UNIX AND NOT APPLE)
    # Create an interface target for the specific compile option
    add_library(warn_suppress_format_security INTERFACE)
    target_compile_options(warn_suppress_format_security INTERFACE "-Wno-format-security")

    # Link this interface target to the file viewdbg.c only
    set_source_files_properties(src/viewdbg.c PROPERTIES
        COMPILE_OPTIONS $<TARGET_PROPERTY:warn_suppress_format_security,INTERFACE_COMPILE_OPTIONS>
    )
endif()

target_compile_definitions(${PROJECT_NAME}
    PRIVATE $<$<PLATFORM_ID:Windows>:-D_CRT_SECURE_NO_WARNINGS>
)

# Copy DLLs on Windows if using vcpkg
if(WIN32)
    # Define _VCPKG_INSTALLED_DIR if it is not already set
    if(NOT DEFINED _VCPKG_INSTALLED_DIR)
        set(_VCPKG_INSTALLED_DIR "C:/path/to/vcpkg/installed/x64-windows")
    endif()

    # Locate the directory where vcpkg has installed the DLLs
    file(GLOB DLLS "${_VCPKG_INSTALLED_DIR}/bin/*.dll")

    foreach(DLL ${DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}"
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
        )
    endforeach()
endif()

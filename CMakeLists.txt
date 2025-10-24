#-----------------------------------------------------------------------------
# Stefan Wessels, November 2024
# Builds an Apple ][+ and //e Emhanced emulator, called apple2
# Builds a 6502 assembler, called asm6502

cmake_minimum_required(VERSION 3.16)

# Set project name
project(a2m LANGUAGES C)

# Find SDL2, and SDL2_mixer
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)

#-----------------------------------------------------------------------------
# EMULATOR with some sources shared with ASSEMBLER below

# Add the executable
add_executable(apple2
    src/6502.c
    src/6502_core.c
    src/apple2.c
    src/asm6502.c
    src/asmexpr.c
    src/asmgperf.c
    src/breakpnt.c
    src/dbgopcds.c
    src/diskii.c
    src/dynarray.c
    src/errorlog.c
    src/frankdisp.c
    src/image.c
    src/inifiles.c
    src/leds.c
    src/main.c
    src/ramcard.c
    src/roms.c
    src/slot.c
    src/smrtprt.c
    src/speaker.c
    src/util.c
    src/viewapl2.c
    src/viewcpu.c
    src/viewdbg.c
    src/viewdlg.c
    src/viewmem.c
    src/viewmisc.c
    src/viewport.c
)

target_compile_definitions(apple2 PRIVATE IS_EMULATOR)
# Add this to be able to use printf
# target_link_options(apple2 PRIVATE $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>)

# Link SDL2, SDL2main, and SDL2_mixer libraries
target_link_libraries(apple2
    SDL2::SDL2
    SDL2::SDL2main
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
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

if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;/Library/Frameworks")
    set_target_properties(apple2 PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks;/Library/Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

target_compile_definitions(apple2
    PRIVATE $<$<PLATFORM_ID:Windows>:-D_CRT_SECURE_NO_WARNINGS>
)

# Copy DLLs on Windows if using vcpkg
if(WIN32)
    # Make this a Windows
    set_target_properties(apple2 PROPERTIES WIN32_EXECUTABLE ON)

    # Define _VCPKG_INSTALLED_DIR if it is not already set
    if(NOT DEFINED _VCPKG_INSTALLED_DIR)
        set(_VCPKG_INSTALLED_DIR "C:/path/to/vcpkg/installed/x64-windows")
    endif()

    # Locate the directory where vcpkg has installed the DLLs
    file(GLOB DLLS "${_VCPKG_INSTALLED_DIR}/bin/*.dll")

    foreach(DLL ${DLLS})
        add_custom_command(TARGET apple2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}"
                $<TARGET_FILE_DIR:apple2>
        )
    endforeach()
endif()

#-----------------------------------------------------------------------------
# ASSEMBLER with shared sources

# Add the executable
add_executable(asm6502
    src/asm6502.c
    src/asm6502.h
    src/asmexpr.c
    src/asmexpr.h
    src/asmgperf.c
    src/asmgperf.h
    src/dynarray.c
    src/errorlog.c
    src/errorlog.h
    src/util.c
)

target_compile_definitions(asm6502 PRIVATE IS_ASSEMBLER)

target_link_libraries(asm6502
    $<$<PLATFORM_ID:Linux>:m>
)

# RelWithDebInfo gets special flags for profiling
foreach(tgt IN ITEMS apple2 asm6502)
  if(TARGET ${tgt})
    if(MSVC)
      target_compile_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/O2;/Zi;/Oy->)
      target_link_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:-O2;-g;-fno-omit-frame-pointer;-fno-optimize-sibling-calls>)
    endif()
  endif()
endforeach()

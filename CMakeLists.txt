#-----------------------------------------------------------------------------
# Stefan Wessels, November 2024
# Builds an Apple ][+ and //e Emhanced emulator, called a2m
# Builds a 6502 assembler, called asm6502

cmake_minimum_required(VERSION 3.16)

# Set project name
project(a2m LANGUAGES C)

# Find SDL2, and SDL2_mixer
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)

# LIBRARIES
add_subdirectory(src/utils)
add_subdirectory(src/asm)
add_subdirectory(src/hardware)
add_subdirectory(src/runtime)
add_subdirectory(src/ui)

#-----------------------------------------------------------------------------
# EMULATOR

# Add the executable
add_executable(a2m
    src/apps/a2m/main.c
)

# Link SDL2, SDL2main, and SDL2_mixer libraries
target_link_libraries(a2m PUBLIC
    # utils
    # asm
    # hardware
    # runtime
    ui
    # SDL2::SDL2
    # SDL2::SDL2main
    # $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
    # $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>
    $<$<PLATFORM_ID:Linux>:m>
)

# Use this to compile only unk_dasm.c with -Wno-format-security on Linux
if(UNIX AND NOT APPLE)
    # Create an interface target for the specific compile option
    add_library(warn_suppress_format_security INTERFACE)
    target_compile_options(warn_suppress_format_security INTERFACE "-Wno-format-security")

    # Link this interface target to the file unk_dasm.c only
    set_source_files_properties(src/unk_dasm.c PROPERTIES
        COMPILE_OPTIONS $<TARGET_PROPERTY:warn_suppress_format_security,INTERFACE_COMPILE_OPTIONS>
    )
endif()

if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks;/Library/Frameworks")
    set_target_properties(a2m PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks;/Library/Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

# Copy DLLs on Windows if using vcpkg
if(WIN32)
    # Add this to so printf works
    target_link_options(a2m PRIVATE /SUBSYSTEM:CONSOLE)

    # target_compile_definitions(a2m
    #     PRIVATE $<$<PLATFORM_ID:Windows>:-D_CRT_SECURE_NO_WARNINGS>
    # )

    # Make this a Windows
    set_target_properties(a2m PROPERTIES WIN32_EXECUTABLE ON)

    # Define _VCPKG_INSTALLED_DIR if it is not already set
    if(NOT DEFINED _VCPKG_INSTALLED_DIR)
        set(_VCPKG_INSTALLED_DIR "C:/path/to/vcpkg/installed/x64-windows")
    endif()

    # Locate the directory where vcpkg has installed the DLLs
    file(GLOB DLLS "${_VCPKG_INSTALLED_DIR}/bin/*.dll")

    foreach(DLL ${DLLS})
        add_custom_command(TARGET a2m POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLL}"
                $<TARGET_FILE_DIR:a2m>
        )
    endforeach()
endif()

#-----------------------------------------------------------------------------
# ASSEMBLER with shared sources

# Add the executable
add_executable(asm6502
    src/apps/asm6502/main.c
)

target_link_libraries(asm6502 PUBLIC
    utils
    asm
    $<$<PLATFORM_ID:Linux>:m>
)

# MSVC gets better warnings and RelWithDebInfo gets profiling flags
foreach(tgt IN ITEMS a2m asm6502)
  if(TARGET ${tgt})
    if(MSVC)
      # Always-on MSVC flags (all configs)
      target_compile_options(${tgt} PRIVATE
        /W4           # high warnings
        # /WX           # warnings as errors
        /we4013       # error on "function undefined; assuming extern returning int"
        /std:c11      # modern C mode
      )

      # RelWithDebInfo gets special flags for profiling
      target_compile_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/O2;/Zi;/Oy->
      )
      target_link_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>
      )

    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
      target_compile_options(${tgt} PRIVATE
        $<$<CONFIG:RelWithDebInfo>:-O2;-g;-fno-omit-frame-pointer;-fno-optimize-sibling-calls>
      )
    endif()
  endif()
endforeach()

# src/frontend/CMakeLists.txt

set(A2M_HELP_CONTENT "${CMAKE_CURRENT_BINARY_DIR}/generated/help_content.inc")
set(A2M_HELP_SECTION_LEVEL "2" CACHE STRING "Markdown heading level used for help sections")

add_custom_command(
    OUTPUT "${A2M_HELP_CONTENT}"
    COMMAND Python3::Interpreter
        "${CMAKE_SOURCE_DIR}/tools/gen_help.py"
        --level "${A2M_HELP_SECTION_LEVEL}"
        "${CMAKE_SOURCE_DIR}/manual/manual.md"
        "${A2M_HELP_CONTENT}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/manual/manual.md"
        "${CMAKE_SOURCE_DIR}/tools/gen_help.py"
    VERBATIM
)

add_custom_target(frontend_help_content DEPENDS "${A2M_HELP_CONTENT}")

add_library(frontend STATIC
    debugger_layout.c
    debugger_layout.h
    crt_renderer.c
    crt_renderer.h
    disk_led_data.c
    disk_led_data.h
    frontend.c
    frontend.h
    frontend_input.c
    frontend_input.h
    frontend_joystick_input.c
    frontend_joystick_input.h
    help_view.c
    help_view.h
    memory_search.c
    memory_search.h
    window_title.c
    window_title.h
    "${A2M_HELP_CONTENT}"
    nuklear_impl.c
    nuklear_config.h
    nuklear.h
    nuklear_sdl.h
)

add_dependencies(frontend frontend_help_content)

target_include_directories(frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_compile_features(frontend PUBLIC c_std_99)

if(MSVC)
    # nuklear.h/nuklear_sdl.h are vendored third-party headers; these MSVC
    # diagnostics fire on intentional, standard-legal idioms there (the
    # pre-C11 alignof-emulation trick, and enum-family flag combinations)
    # that we don't want to alter upstream.
    target_compile_options(frontend PRIVATE /wd4116 /wd5287)
endif()

# Public headers include platform.h / SDL.h / runtime_*. runtime already
# PUBLIC-links machine (and util via machine) — do not re-list those or
# executables get duplicate -l lines on Apple ld.
target_link_libraries(frontend
    PUBLIC
        runtime
        platform
        SDL2::SDL2
    PRIVATE
        a2m_tiny_regex
        a2m_stb_image
        tools
)

if(UNIX AND NOT APPLE)
    target_link_libraries(frontend PRIVATE m)
endif()

cmake_minimum_required(VERSION 3.16)

project(a2m
    VERSION 3.0.0
    LANGUAGES C
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
    add_compile_definitions(SDL_MAIN_HANDLED)
    add_link_options(/STACK:8388608)
endif()

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive")
endif()

if(ENABLE_ASAN)
    message(STATUS "Building with AddressSanitizer")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

if(ENABLE_TSAN)
    message(STATUS "Building with ThreadSanitizer")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

enable_testing()

find_package(SDL2 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(external)

add_subdirectory(src/util)
add_subdirectory(src/machine)
add_subdirectory(src/runtime)
add_subdirectory(src/platform)
add_subdirectory(src/control)
add_subdirectory(src/tools)
add_subdirectory(src/frontend)

# Product binary: debugger UI + Apple II machine.
add_executable(a2m
    src/app_options.c
    src/main.c
)

target_compile_features(a2m PRIVATE c_std_99)
target_compile_definitions(a2m PRIVATE
    SDL_MAIN_HANDLED
    A2M_VERSION="${PROJECT_VERSION}"
    A2M_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(a2m PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

# Direct deps only: frontend PUBLIC-links runtime→machine→util and PRIVATE-links
# platform/tools/SDL. Re-listing those here duplicates on Apple ld.
target_link_libraries(a2m PRIVATE
    a2m_argparse
    frontend
    control
)

# --- util ------------------------------------------------------------------

add_executable(test_audio_buffer
    tests/util/test_audio_buffer.c
)
target_compile_features(test_audio_buffer PRIVATE c_std_99)
target_link_libraries(test_audio_buffer PRIVATE util SDL2::SDL2)
add_test(NAME audio_buffer COMMAND test_audio_buffer)

add_executable(test_message_queue
    tests/util/test_message_queue.c
)
target_compile_features(test_message_queue PRIVATE c_std_99)
target_link_libraries(test_message_queue PRIVATE util SDL2::SDL2)
add_test(NAME message_queue COMMAND test_message_queue)

add_executable(test_memory_search
    tests/frontend/test_memory_search.c
    src/frontend/memory_search.c
)
target_include_directories(test_memory_search PRIVATE
    ${CMAKE_SOURCE_DIR}/src/frontend
)
target_compile_features(test_memory_search PRIVATE c_std_99)
add_test(NAME memory_search COMMAND test_memory_search)

add_executable(test_disasm_pc_lock
    tests/frontend/test_disasm_pc_lock.c
    src/frontend/disasm_pc_lock.c
)
target_include_directories(test_disasm_pc_lock PRIVATE
    ${CMAKE_SOURCE_DIR}/src/frontend
)
target_compile_features(test_disasm_pc_lock PRIVATE c_std_99)
target_link_libraries(test_disasm_pc_lock PRIVATE tools)
add_test(NAME disasm_pc_lock COMMAND test_disasm_pc_lock)

add_executable(test_help_view
    tests/frontend/test_help_view.c
)
target_include_directories(test_help_view PRIVATE
    ${CMAKE_SOURCE_DIR}/src/frontend
    ${CMAKE_SOURCE_DIR}/src/machine
)
target_compile_features(test_help_view PRIVATE c_std_99)
target_compile_definitions(test_help_view PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(test_help_view PRIVATE frontend)
add_test(NAME help_view COMMAND test_help_view)

add_executable(test_frontend_input
    tests/frontend/test_frontend_input.c
    src/frontend/frontend_input.c
)
target_include_directories(test_frontend_input PRIVATE
    ${CMAKE_SOURCE_DIR}/src/frontend
    ${CMAKE_SOURCE_DIR}/src/machine
)
target_compile_features(test_frontend_input PRIVATE c_std_99)
target_link_libraries(test_frontend_input PRIVATE machine SDL2::SDL2)
add_test(NAME frontend_input COMMAND test_frontend_input)

add_executable(test_apple_type_script
    tests/util/test_apple_type_script.c
)
target_compile_features(test_apple_type_script PRIVATE c_std_99)
target_link_libraries(test_apple_type_script PRIVATE util)
add_test(NAME apple_type_script COMMAND test_apple_type_script)

add_executable(test_apple2_file
    tests/util/test_apple2_file.c
)
target_compile_features(test_apple2_file PRIVATE c_std_99)
target_link_libraries(test_apple2_file PRIVATE util)
add_test(NAME apple2_file COMMAND test_apple2_file)

# --- machine ---------------------------------------------------------------

add_executable(test_apple2_stub
    tests/machine/test_apple2_stub.c
)
target_compile_features(test_apple2_stub PRIVATE c_std_99)
target_link_libraries(test_apple2_stub PRIVATE machine)
add_test(NAME apple2_stub COMMAND test_apple2_stub)

add_executable(test_cpu65_basic
    tests/machine/test_cpu65_basic.c
)
target_compile_features(test_cpu65_basic PRIVATE c_std_99)
target_link_libraries(test_cpu65_basic PRIVATE machine)
add_test(NAME cpu65_basic COMMAND test_cpu65_basic)

add_executable(test_softswitch
    tests/machine/test_softswitch.c
)
target_compile_features(test_softswitch PRIVATE c_std_99)
target_link_libraries(test_softswitch PRIVATE machine)
add_test(NAME softswitch COMMAND test_softswitch)

add_executable(test_rom_boot
    tests/machine/test_rom_boot.c
)
target_compile_features(test_rom_boot PRIVATE c_std_99)
target_link_libraries(test_rom_boot PRIVATE machine)
add_test(NAME rom_boot COMMAND test_rom_boot)

add_executable(test_video_beam
    tests/machine/test_video_beam.c
)
target_compile_features(test_video_beam PRIVATE c_std_99)
target_link_libraries(test_video_beam PRIVATE machine)
add_test(NAME video_beam COMMAND test_video_beam)

add_executable(test_video_block_paint
    tests/machine/test_video_block_paint.c
)
target_compile_features(test_video_block_paint PRIVATE c_std_99)
target_link_libraries(test_video_block_paint PRIVATE machine)
add_test(NAME video_block_paint COMMAND test_video_block_paint)

add_executable(test_diskii
    tests/machine/test_diskii.c
)
target_compile_features(test_diskii PRIVATE c_std_99)
target_compile_definitions(test_diskii PRIVATE
    A2M_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(test_diskii PRIVATE machine)
add_test(NAME diskii COMMAND test_diskii)

add_executable(test_peripherals
    tests/machine/test_peripherals.c
)
target_compile_features(test_peripherals PRIVATE c_std_99)
target_link_libraries(test_peripherals PRIVATE machine)
add_test(NAME peripherals COMMAND test_peripherals)

add_executable(test_hostfs
    tests/machine/test_hostfs.c
)
target_compile_features(test_hostfs PRIVATE c_std_99)
target_compile_definitions(test_hostfs PRIVATE
    A2M_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(test_hostfs PRIVATE machine)
add_test(NAME hostfs COMMAND test_hostfs)

add_executable(test_cxxx_map
    tests/machine/test_cxxx_map.c
)
target_compile_features(test_cxxx_map PRIVATE c_std_99)
target_link_libraries(test_cxxx_map PRIVATE machine)
add_test(NAME cxxx_map COMMAND test_cxxx_map)

add_executable(test_memview
    tests/machine/test_memview.c
)
target_compile_features(test_memview PRIVATE c_std_99)
target_link_libraries(test_memview PRIVATE machine)
add_test(NAME memview COMMAND test_memview)

add_executable(test_apple2_snapshot
    tests/machine/test_apple2_snapshot.c
)
target_compile_features(test_apple2_snapshot PRIVATE c_std_99)
target_link_libraries(test_apple2_snapshot PRIVATE machine)
add_test(NAME apple2_snapshot COMMAND test_apple2_snapshot)

add_executable(bench_realtime
    tests/machine/bench_realtime.c
)
target_compile_features(bench_realtime PRIVATE c_std_99)
target_link_libraries(bench_realtime PRIVATE machine)

# --- product options / CLI smoke ------------------------------------------

add_test(NAME a2m_help COMMAND a2m --help)
set_tests_properties(a2m_help PROPERTIES PASS_REGULAR_EXPRESSION "a2m")

add_test(NAME a2m_version COMMAND a2m --version)
set_tests_properties(a2m_version PROPERTIES PASS_REGULAR_EXPRESSION "a2m 3\\.")

add_test(NAME a2m_headless COMMAND a2m --noini --headless)
set_tests_properties(a2m_headless PROPERTIES TIMEOUT 15)

add_executable(test_app_options_mounts
    tests/test_app_options_mounts.c
    src/app_options.c
)
target_include_directories(test_app_options_mounts PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/util
    ${CMAKE_SOURCE_DIR}/external/argparse
)
target_compile_features(test_app_options_mounts PRIVATE c_std_99)
target_link_libraries(test_app_options_mounts PRIVATE util a2m_argparse)
add_test(NAME app_options_mounts COMMAND test_app_options_mounts)

# --- runtime ---------------------------------------------------------------

add_executable(test_runtime_stepping
    tests/runtime/test_runtime_stepping.c
)
target_compile_definitions(test_runtime_stepping PRIVATE
    A2M_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
target_compile_features(test_runtime_stepping PRIVATE c_std_99)
# util comes via runtime → machine; do not re-list (Apple ld duplicate warning).
target_link_libraries(test_runtime_stepping PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_stepping COMMAND test_runtime_stepping)

add_executable(test_runtime_display_stop
    tests/runtime/test_runtime_display_stop.c
)
target_compile_features(test_runtime_display_stop PRIVATE c_std_99)
target_link_libraries(test_runtime_display_stop PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_display_stop COMMAND test_runtime_display_stop)

add_executable(test_runtime_smartport_boot
    tests/runtime/test_runtime_smartport_boot.c
)
target_compile_features(test_runtime_smartport_boot PRIVATE c_std_99)
target_link_libraries(test_runtime_smartport_boot PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_smartport_boot COMMAND test_runtime_smartport_boot)

add_executable(test_runtime_step_nested
    tests/runtime/test_runtime_step_nested.c
)
target_compile_features(test_runtime_step_nested PRIVATE c_std_99)
target_link_libraries(test_runtime_step_nested PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_step_nested COMMAND test_runtime_step_nested)

add_executable(test_runtime_memory_rpc
    tests/runtime/test_runtime_memory_rpc.c
)
target_compile_features(test_runtime_memory_rpc PRIVATE c_std_99)
target_link_libraries(test_runtime_memory_rpc PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_memory_rpc COMMAND test_runtime_memory_rpc)

add_executable(test_runtime_breakpoint
    tests/runtime/test_runtime_breakpoint.c
)
target_compile_features(test_runtime_breakpoint PRIVATE c_std_99)
target_link_libraries(test_runtime_breakpoint PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_breakpoint COMMAND test_runtime_breakpoint)

add_executable(test_runtime_breakpoint_ini
    tests/runtime/test_runtime_breakpoint_ini.c
)
target_compile_features(test_runtime_breakpoint_ini PRIVATE c_std_99)
target_link_libraries(test_runtime_breakpoint_ini PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_breakpoint_ini COMMAND test_runtime_breakpoint_ini)

add_executable(test_runtime_turbo
    tests/runtime/test_runtime_turbo.c
)
target_compile_features(test_runtime_turbo PRIVATE c_std_99)
target_link_libraries(test_runtime_turbo PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_turbo COMMAND test_runtime_turbo)

add_executable(test_runtime_slot_resolve
    tests/runtime/test_runtime_slot_resolve.c
)
target_compile_features(test_runtime_slot_resolve PRIVATE c_std_99)
target_link_libraries(test_runtime_slot_resolve PRIVATE runtime)
add_test(NAME runtime_slot_resolve COMMAND test_runtime_slot_resolve)

add_executable(test_runtime_savestate
    tests/runtime/test_runtime_savestate.c
)
target_compile_features(test_runtime_savestate PRIVATE c_std_99)
target_link_libraries(test_runtime_savestate PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_savestate COMMAND test_runtime_savestate)

add_executable(test_runtime_machine_files
    tests/runtime/test_runtime_machine_files.c
)
target_compile_features(test_runtime_machine_files PRIVATE c_std_99)
target_link_libraries(test_runtime_machine_files PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_machine_files COMMAND test_runtime_machine_files)

# Frame ring unit (ARGB product path).
add_executable(test_runtime_frame_ring
    tests/runtime/test_runtime_frame_ring.c
)
target_compile_features(test_runtime_frame_ring PRIVATE c_std_99)
target_link_libraries(test_runtime_frame_ring PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_frame_ring COMMAND test_runtime_frame_ring)

# CPU flight recorder basic integration (C3).
add_executable(test_runtime_history_basic
    tests/runtime/test_runtime_history_basic.c
)
target_compile_features(test_runtime_history_basic PRIVATE c_std_99)
target_link_libraries(test_runtime_history_basic PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_history_basic COMMAND test_runtime_history_basic)

# History INFO/RECORD/CLEAR command path (C4a).
add_executable(test_runtime_history_commands
    tests/runtime/test_runtime_history_commands.c
)
target_compile_features(test_runtime_history_commands PRIVATE c_std_99)
target_link_libraries(test_runtime_history_commands PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_history_commands COMMAND test_runtime_history_commands)

# History FIND/READ/CLOSE + HST1 (C4b).
add_executable(test_runtime_history_query
    tests/runtime/test_runtime_history_query.c
)
target_compile_features(test_runtime_history_query PRIVATE c_std_99)
target_link_libraries(test_runtime_history_query PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_history_query COMMAND test_runtime_history_query)

add_executable(test_runtime_history_sessions
    tests/runtime/test_runtime_history_sessions.c
)
target_compile_features(test_runtime_history_sessions PRIVATE c_std_99)
target_link_libraries(test_runtime_history_sessions PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_history_sessions COMMAND test_runtime_history_sessions)

add_executable(test_runtime_state_changed
    tests/runtime/test_runtime_state_changed.c
)
target_compile_features(test_runtime_state_changed PRIVATE c_std_99)
target_link_libraries(test_runtime_state_changed PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_state_changed COMMAND test_runtime_state_changed)

add_executable(test_runtime_timemachine
    tests/runtime/test_runtime_timemachine.c
)
target_compile_features(test_runtime_timemachine PRIVATE c_std_99)
target_link_libraries(test_runtime_timemachine PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_timemachine COMMAND test_runtime_timemachine)

add_executable(test_runtime_tm_replay
    tests/runtime/test_runtime_tm_replay.c
)
target_compile_features(test_runtime_tm_replay PRIVATE c_std_99)
target_link_libraries(test_runtime_tm_replay PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_tm_replay COMMAND test_runtime_tm_replay)

add_executable(test_runtime_tm_forensic
    tests/runtime/test_runtime_tm_forensic.c
)
target_compile_features(test_runtime_tm_forensic PRIVATE c_std_99)
target_link_libraries(test_runtime_tm_forensic PRIVATE control runtime SDL2::SDL2)
add_test(NAME runtime_tm_forensic COMMAND test_runtime_tm_forensic)

add_executable(test_runtime_tm_bp
    tests/runtime/test_runtime_tm_bp.c
)
target_compile_features(test_runtime_tm_bp PRIVATE c_std_99)
target_link_libraries(test_runtime_tm_bp PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_tm_bp COMMAND test_runtime_tm_bp)

# Control protocol (A2M product wire — src/control).
add_executable(test_control_protocol
    tests/control/test_control_protocol.c
)
target_compile_features(test_control_protocol PRIVATE c_std_99)
target_link_libraries(test_control_protocol PRIVATE control)
add_test(NAME control_protocol COMMAND test_control_protocol)

# --- tools / assembler -----------------------------------------------------

add_executable(test_assembler_expressions
    tests/tools/test_assembler_expressions.c
)
target_compile_features(test_assembler_expressions PRIVATE c_std_99)
target_link_libraries(test_assembler_expressions PRIVATE assembler)
add_test(NAME assembler_expressions COMMAND test_assembler_expressions)

add_executable(test_assembler_conditionals
    tests/tools/test_assembler_conditionals.c
)
target_compile_features(test_assembler_conditionals PRIVATE c_std_99)
target_link_libraries(test_assembler_conditionals PRIVATE assembler)
add_test(NAME assembler_conditionals COMMAND test_assembler_conditionals)

add_executable(test_assembler_loops
    tests/tools/test_assembler_loops.c
)
target_compile_features(test_assembler_loops PRIVATE c_std_99)
target_link_libraries(test_assembler_loops PRIVATE assembler)
add_test(NAME assembler_loops COMMAND test_assembler_loops)

add_executable(test_assembler_macros
    tests/tools/test_assembler_macros.c
)
target_compile_features(test_assembler_macros PRIVATE c_std_99)
target_link_libraries(test_assembler_macros PRIVATE assembler)
add_test(NAME assembler_macros COMMAND test_assembler_macros)

add_executable(test_assembler_scopes_segments
    tests/tools/test_assembler_scopes_segments.c
)
target_compile_features(test_assembler_scopes_segments PRIVATE c_std_99)
target_link_libraries(test_assembler_scopes_segments PRIVATE assembler)
add_test(NAME assembler_scopes_segments COMMAND test_assembler_scopes_segments)

add_executable(test_assembler_targets
    tests/tools/test_assembler_targets.c
)
target_compile_features(test_assembler_targets PRIVATE c_std_99)
target_link_libraries(test_assembler_targets PRIVATE assembler)
add_test(NAME assembler_targets COMMAND test_assembler_targets)

add_executable(test_disasm_6502
    tests/tools/test_disasm_6502.c
)
target_compile_features(test_disasm_6502 PRIVATE c_std_99)
target_link_libraries(test_disasm_6502 PRIVATE disasm_6502)
add_test(NAME disasm_6502 COMMAND test_disasm_6502)

add_executable(test_assembler_65c02
    tests/tools/test_assembler_65c02.c
)
target_compile_features(test_assembler_65c02 PRIVATE c_std_99)
target_link_libraries(test_assembler_65c02 PRIVATE assembler)
add_test(NAME assembler_65c02 COMMAND test_assembler_65c02)

add_executable(test_assembler_multifile
    tests/tools/test_assembler_multifile.c
)
target_compile_features(test_assembler_multifile PRIVATE c_std_99)
target_compile_definitions(test_assembler_multifile PRIVATE
    A2M_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/asm"
)
target_link_libraries(test_assembler_multifile PRIVATE assembler)
add_test(NAME assembler_multifile COMMAND test_assembler_multifile)

add_executable(test_runtime_assembler
    tests/runtime/test_runtime_assembler.c
)
target_compile_features(test_runtime_assembler PRIVATE c_std_99)
target_link_libraries(test_runtime_assembler PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_assembler COMMAND test_runtime_assembler)

add_executable(test_runtime_assembler_mli
    tests/runtime/test_runtime_assembler_mli.c
)
target_compile_features(test_runtime_assembler_mli PRIVATE c_std_99)
target_link_libraries(test_runtime_assembler_mli PRIVATE runtime SDL2::SDL2)
add_test(NAME runtime_assembler_mli COMMAND test_runtime_assembler_mli)

add_test(
    NAME am65_cli_65c02
    COMMAND $<TARGET_FILE:am65> --cpu 65c02
        -i ${CMAKE_SOURCE_DIR}/tests/fixtures/am65_65c02.asm
)
add_test(
    NAME am65_cli_default_rejects_65c02
    COMMAND $<TARGET_FILE:am65>
        -i ${CMAKE_SOURCE_DIR}/tests/fixtures/am65_65c02.asm
)
set_tests_properties(am65_cli_default_rejects_65c02 PROPERTIES WILL_FAIL TRUE)
add_test(
    NAME am65_cli_wdc
    COMMAND $<TARGET_FILE:am65> --cpu wdc
        -i ${CMAKE_SOURCE_DIR}/tests/fixtures/am65_wdc.asm
)
add_test(
    NAME am65_cli_rockwell_rejects_wdc
    COMMAND $<TARGET_FILE:am65> --cpu rockwell
        -i ${CMAKE_SOURCE_DIR}/tests/fixtures/am65_wdc.asm
)
set_tests_properties(am65_cli_rockwell_rejects_wdc PROPERTIES WILL_FAIL TRUE)

# frontend_help.c was replaced by help_view + generated content in the wholesale
# shell; re-add a dedicated help unit test when the public help API stabilizes.

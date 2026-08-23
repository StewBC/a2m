# Apple-backed runtime. History + frame ring are product remote-debug paths.

add_library(runtime STATIC
    runtime_client.c
    runtime_breakpoint_condition.c
    runtime_breakpoint_ini.c
    runtime_command.c
    runtime_event.c
    runtime_frame_ring.c
    runtime_history.c
    runtime_history_wire.c
    runtime_assembler.c
    runtime_slot_resolve.c
    runtime.c
    runtime_thread.c
    runtime_timemachine.c
    runtime_tm_recorder.c
)

target_include_directories(runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(runtime PUBLIC c_std_99)

# machine already PUBLIC-links util; listing util here again puts it twice on
# consumer link lines (Apple ld: "ignoring duplicate libraries").
target_link_libraries(runtime
    PUBLIC
        machine
        symbols
    PRIVATE
        SDL2::SDL2
        assembler
)

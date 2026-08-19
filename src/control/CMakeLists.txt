# src/control/CMakeLists.txt
# Wire protocol / server. Dispatch uses runtime_client only.

add_library(control STATIC
    control_server.c
    control_protocol.c
    control_deferred.c
    control_dispatch.c
    control_breakpoint.c
)

target_include_directories(control PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(control PUBLIC c_std_99)

target_link_libraries(control
    PUBLIC
        util
        platform
        runtime
)

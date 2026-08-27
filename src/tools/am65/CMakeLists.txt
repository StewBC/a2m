# am65: shared 6502/65C02 assembler library and standalone command-line tool.

add_library(am65lib STATIC
    asm.c
    define.c
    dynarray.c
    emit.c
    errorlog.c
    err.c
    expr.c
    file.c
    gperf.c
    opcode.c
    parse.c
    scope.c
    segment.c
    symbol.c
    token.c
)

target_include_directories(am65lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(am65lib PUBLIC c_std_99)

# Keep the consumer-facing target name stable across a2m and c64m.
add_library(assembler ALIAS am65lib)

add_executable(am65 main.c)
target_compile_features(am65 PRIVATE c_std_99)
target_link_libraries(am65 PRIVATE am65lib)
set_target_properties(am65 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

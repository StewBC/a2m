# src/tools/CMakeLists.txt

add_library(tools INTERFACE)

add_subdirectory(am65)
add_subdirectory(disasm_6502)
add_subdirectory(symbols)

target_link_libraries(tools INTERFACE
    assembler
    disasm_6502
    symbols
)

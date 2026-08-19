# src/tools/disasm_6502/CMakeLists.txt

add_library(disasm_6502 STATIC
    disasm_6502.c
)

target_include_directories(disasm_6502 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(disasm_6502 PUBLIC c_std_99)


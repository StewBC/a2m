# src/tools/symbols/CMakeLists.txt

add_library(symbols STATIC
    symbol_file.c
    symbol_table.c
)

target_include_directories(symbols PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(symbols PUBLIC c_std_99)

target_link_libraries(symbols
    PUBLIC
        disasm_6502
    PRIVATE
        a2m_stb_ds
)

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

extern const char **opcode_text;
extern const char **opcode_hex_params;
extern const char **opcode_symbol_params;
extern const int *opcode_lengths;

extern const char *opcode_text_6502[256];
extern const char *opcode_text_65c02[256];
extern const char *opcode_hex_params_6502[256];
extern const char *opcode_hex_params_65c02[256];
extern const char *opcode_symbol_params_6502[256];
extern const char *opcode_symbol_params_65c02[256];
extern const int opcode_lengths_6502[256];
extern const int opcode_lengths_65c02[256];

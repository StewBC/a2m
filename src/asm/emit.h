// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

enum {
    BYTE_ORDER_LO,                                          // for example .word vs .drow
    BYTE_ORDER_HI,
};

void emit_byte(ASSEMBLER *as, uint8_t byte_value);
void emit_opcode(ASSEMBLER *as);
void emit_values(ASSEMBLER *as, uint64_t value, int width, int order);
void emit_string(ASSEMBLER *as, SYMBOL_LABEL *sl);
void emit_cs_values(ASSEMBLER *as, int width, int order);

// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

enum {
    ADDRESS_MODE_ACCUMULATOR,
    ADDRESS_MODE_ABSOLUTE,
    ADDRESS_MODE_ABSOLUTE_X,
    ADDRESS_MODE_ABSOLUTE_Y,
    ADDRESS_MODE_IMMEDIATE,
    ADDRESS_MODE_INDIRECT_X,
    ADDRESS_MODE_INDIRECT_Y,
    ADDRESS_MODE_INDIRECT,
    ADDRESS_MODE_ZEROPAGE,
    ADDRESS_MODE_ZEROPAGE_X,
    ADDRESS_MODE_ZEROPAGE_Y,
};

extern const uint8_t asm_opcode[GPERF_OPCODE_TYA+1][ADDRESS_MODE_ZEROPAGE_Y+1];
extern const uint8_t asm_opcode_type[GPERF_OPCODE_TYA+1][ADDRESS_MODE_ZEROPAGE_Y+1];
extern const char *address_mode_txt[];

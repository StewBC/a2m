// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stdint.h>

#include "asm_common.h"
#include "gperf.h"

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

extern const uint8_t asm_opcode[GPERF_OPCODE_WAI + 1][ADDRESS_MODE_ZEROPAGE_Y + 1];
/* Compatibility encoding: 1=6502, 0=65C02, 2=Rockwell, 3=WDC, -1=invalid. */
extern const uint8_t asm_opcode_type[GPERF_OPCODE_WAI + 1][ADDRESS_MODE_ZEROPAGE_Y + 1];
extern const char *address_mode_txt[];

int asm_opcode_allowed(
    assembler_cpu_profile profile,
    uint8_t opcode_id,
    uint8_t addressing_mode);
const char *assembler_cpu_profile_name(assembler_cpu_profile profile);

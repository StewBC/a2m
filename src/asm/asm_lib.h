// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "utils_lib.h"
#include "asm6502.h"
#include "asmexpr.h"

#define ASM_ERR_MAX_STR_LEN 256

void asm_err(ASSEMBLER *as, const char *format, ...);
OPCODEINFO *in_word_set(register const char *str, register size_t len);
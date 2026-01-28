// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "utils_lib.h"
#include "asm6502.h"
#include "asmexpr.h"

#define ASM_ERR_MAX_STR_LEN 256

typedef enum{
    ASM_ERR_DEFINE,     // show only on pass 1
    ASM_ERR_RESOLVE,    // show only on pass 2
    ASM_ERR_FATAL       // show always
} ASM_ERR_CLASS;

void asm_err(ASSEMBLER *as, ASM_ERR_CLASS cls, const char *format, ...);
OPCODEINFO *in_word_set(register const char *str, register size_t len);
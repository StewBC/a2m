// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "utils_lib.h"

// Hash buckets are a power of two
#define HASH_BUCKETS    64
#define HASH_MASK       (HASH_BUCKETS - 1)

typedef struct OPCODEINFO {
    const char *mnemonic;                                   // lda, etc.
    uint8_t opcode_id;                                      // GPERF_OPCODE_*
    uint8_t width;                                          // 0 (implied), 1 (relative), 8, 16
    uint8_t addressing_mode;
    uint64_t value;
} OPCODEINFO;

typedef struct ASSEMBLER ASSEMBLER;

#include "asm_err.h"
#include "expr.h"
#include "gperf.h"
#include "incl_fls.h"
#include "opcds.h"
#include "tokens.h"
#include "parse.h"
#include "scope.h"
#include "segment.h"
#include "symbols.h"
#include "emit.h"
#include "asm6502.h"

OPCODEINFO *in_word_set(register const char *str, register size_t len);

// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stdint.h>

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

typedef enum {
    TOKEN_NUM,
    TOKEN_OP,
    TOKEN_VAR,
    TOKEN_STR,
    TOKEN_END,
} TOKENTYPE;

typedef struct {
    TOKENTYPE type;
    int64_t value;
    char op;
    const char *name;
    uint32_t name_length;
    uint32_t name_hash;
} TOKEN;

void get_token(ASSEMBLER *as);
void next_token(ASSEMBLER *as);
int peek_next_op(ASSEMBLER *as, int *out_op);
void expect_op(ASSEMBLER *as, char op);

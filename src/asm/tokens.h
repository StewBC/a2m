// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Define token types
typedef enum {
    TOKEN_NUM,
    TOKEN_OP,
    TOKEN_VAR,
    TOKEN_STR,
    TOKEN_END,
} TOKENTYPE;

// Token structure
typedef struct {
    TOKENTYPE type;                                         // For the expression parser state machine
    int64_t value;
    char op;                                                // Im expressions the operator but also useful for token ID
    const char *name;                                       // Variable name for lookup
    uint32_t name_length;
    uint32_t name_hash;
} TOKEN;

void get_token(ASSEMBLER *as);
void next_token(ASSEMBLER *as);
int peek_next_op(ASSEMBLER *as, int *out_op);
void expect_op(ASSEMBLER *as, char op);

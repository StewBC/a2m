// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

void get_token(ASSEMBLER *as);
void next_token(ASSEMBLER *as);
int64_t exponentiation_by_squaring(int64_t base, int64_t exp);
int64_t parse_primary(ASSEMBLER *as);
int64_t parse_factor(ASSEMBLER *as);
int64_t parse_exponentiation(ASSEMBLER *as);
int64_t parse_term(ASSEMBLER *as);
int64_t parse_additive(ASSEMBLER *as);
int64_t parse_shift(ASSEMBLER *as);
int64_t parse_bitwise_and(ASSEMBLER *as);
int64_t parse_bitwise_xor(ASSEMBLER *as);
int64_t parse_bitwise_or(ASSEMBLER *as);
int64_t parse_relational(ASSEMBLER *as);
int64_t parse_logical(ASSEMBLER *as);
int64_t parse_conditional(ASSEMBLER *as, int64_t condition_value);
int64_t parse_expression(ASSEMBLER *as);
int64_t evaluate_expression(ASSEMBLER *as);

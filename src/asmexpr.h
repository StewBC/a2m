#pragma once

void get_token();
void next_token();
int64_t exponentiation_by_squaring(int64_t base, int64_t exp);
int64_t parse_primary();
int64_t parse_factor();
int64_t parse_exponentiation();
int64_t parse_term();
int64_t parse_additive();
int64_t parse_shift();
int64_t parse_bitwise_and();
int64_t parse_bitwise_xor();
int64_t parse_bitwise_or();
int64_t parse_relational();
int64_t parse_logical();
int64_t parse_conditional(int64_t condition_value);
int64_t parse_expression();
int64_t evaluate_expression();

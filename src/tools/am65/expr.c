// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

static int64_t expr_exponentiation_by_squaring(int64_t base, int64_t exp) {
    int64_t result = 1;
    while(exp > 0) {
        if(exp & 1) {
            result *= base;
        }
        exp >>= 1;
        base *= base;
    }
    return result;
}

static int anonymous_symbol_lookup(ASSEMBLER *as, uint16_t *address, int direction) {
    if(as->anon_symbols.items == 0) {
        *address = 0xFFFF;
        return 0;
    }

    int low = 0;
    int high = (int)as->anon_symbols.items - 1;
    int exact_match_index = -1;
    int closest_smaller_index = -1;
    int target_index;

    while(low <= high) {
        int mid = (low + high) / 2;
        uint16_t value = *AM65_ARRAY_GET(&as->anon_symbols, uint16_t, mid);

        if(value == *address) {
            exact_match_index = mid;
            break;
        } else if(value < *address) {
            closest_smaller_index = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if(exact_match_index != -1) {
        target_index = exact_match_index + direction;
    } else if(direction < 0) {
        target_index = closest_smaller_index + 1 + direction;
    } else {
        target_index = closest_smaller_index + direction;
    }

    if(target_index < 0 || target_index >= (int)as->anon_symbols.items) {
        *address = 0xFFFF;
        return 0;
    }

    *address = *AM65_ARRAY_GET(&as->anon_symbols, uint16_t, target_index);
    return 1;
}

static uint16_t expr_anonymous_address(ASSEMBLER *as) {
    /* After the leading ':', require '+' or '-' (:+ / :++ / :- / :--).
       A bare ":" alone is not a reference (use ":-" for the previous
       anonymous label). Never printf token.op with %c when it may be '\0'
       (TOKEN_END): that used to embed a NUL and truncate the error at '('. */
    next_token(as);
    char op = as->token.op;
    int direction = 0;

    if(as->token.type != TOKEN_OP || (op != '+' && op != '-')) {
        if(as->token.type == TOKEN_END) {
            asm_err(as, ASM_ERR_RESOLVE,
                    "Anonymous label reference needs '+' or '-' after ':' (e.g. ':-' or ':+')");
        } else if(as->token.type == TOKEN_OP) {
            asm_err(as, ASM_ERR_RESOLVE,
                    "Unexpected symbol after anonymous ':' (expected '+' or '-', got '%c')",
                    op ? op : '?');
        } else {
            asm_err(as, ASM_ERR_RESOLVE,
                    "Unexpected token after anonymous ':' (expected '+' or '-')");
        }
        return 0;
    }

    while(as->token.type == TOKEN_OP && op == as->token.op) {
        direction++;
        next_token(as);
    }

    if(op == '-') {
        direction = -direction;
    }

    uint16_t address = current_output_address(as) + 1;
    if(!anonymous_symbol_lookup(as, &address, direction)) {
        asm_err(as, ASM_ERR_RESOLVE, "Invalid anonymous label address");
    }
    return address;
}

static int64_t expr_primary(ASSEMBLER *as) {
    int64_t value;
    if(as->token.type == TOKEN_OP && as->token.op == '*') {
        next_token(as);
        value = current_output_address(as);
    } else if(as->token.type == TOKEN_OP && as->token.op == ':') {
        value = expr_anonymous_address(as);
    } else if(as->token.type == TOKEN_OP && as->token.op == 'D') {
        value = 0;
        next_token(as);
        if(as->token.type != TOKEN_END) {
            while(as->token.type != TOKEN_END) {
                next_token(as);
            }
            value = 1;
        }
    } else if(as->token.type == TOKEN_NUM) {
        value = as->token.value;
        next_token(as);
    } else if(as->token.type == TOKEN_VAR) {
        const char *symbol_name = as->token.name;
        uint32_t symbol_len = as->token.name_length;
        SYMBOL_LABEL *sl = symbol_read(as, symbol_name, symbol_len);
        next_token(as);

        if(!sl || sl->symbol_type == SYMBOL_UNKNOWN || sl->symbol_type == SYMBOL_LOCAL) {
            if(as->pass == 1) {
                if(!sl) {
                    sl = symbol_write(as, symbol_name, symbol_len, SYMBOL_UNKNOWN, 0xFFFF);
                    if(!sl) {
                        as->expression_unknown = 1;
                        if(symbol_has_scope_path(symbol_name, (int)symbol_len)) {
                            return 65535;
                        }
                        asm_err(as, ASM_ERR_RESOLVE, "Symbol %.*s could not be created", (int)symbol_len, symbol_name);
                        return 0;
                    }
                }
                as->expression_unknown = 1;
                sl->symbol_width = 16;
            } else {
                if(sl && sl->symbol_type == SYMBOL_UNKNOWN) {
                    SCOPE *s = as->active_scope->parent_scope;
                    while(s) {
                        SYMBOL_LABEL *sl1 = symbol_lookup_chain(s, sl->symbol_hash, sl->symbol_name, sl->symbol_length);
                        if(sl1 && sl1->symbol_type != SYMBOL_UNKNOWN) {
                            as->expression_size = 16;
                            sl->symbol_value = sl1->symbol_value;
                            sl->symbol_type = sl1->symbol_type;
                            break;
                        }
                        s = s->parent_scope;
                    }
                    if(sl->symbol_type == SYMBOL_UNKNOWN) {
                        sl = NULL;
                    }
                } else {
                    sl = NULL;
                }

                if(!sl) {
                    as->expression_unknown = 1;
                    asm_err(as, ASM_ERR_RESOLVE, "Value for %.*s not found", (int)symbol_len, symbol_name);
                    return 0;
                }
            }
        }

        value = sl->symbol_value;

        if(as->token.type == TOKEN_OP) {
            char op = as->token.op;
            if(op == '=') {
                next_token(as);
                if(sl->symbol_type != SYMBOL_ADDRESS) {
                    value = sl->symbol_value = expr_evaluate(as);
                    sl->symbol_type = SYMBOL_VARIABLE;
                    sl->symbol_width |= as->expression_size;
                } else {
                    op = 0;
                }
            } else if(op == '+' || op == '-') {
                if(as->cur && *as->cur == op) {
                    next_token(as);
                    next_token(as);
                    if(sl->symbol_type == SYMBOL_ADDRESS) {
                        op = 0;
                    } else {
                        if(sl->symbol_type == SYMBOL_UNKNOWN) {
                            sl->symbol_type = SYMBOL_VARIABLE;
                        }
                        if(op == '+') {
                            value = ++sl->symbol_value;
                        } else {
                            value = --sl->symbol_value;
                        }
                    }
                }
            }
            if(!op) {
                asm_err(as, ASM_ERR_RESOLVE, "Cannot assign value to label %.*s", (int)sl->symbol_length, sl->symbol_name);
            }
        }

        if(sl->symbol_width > as->expression_size) {
            as->expression_size = sl->symbol_width;
        }
    } else if(as->token.type == TOKEN_OP && as->token.op == '(') {
        next_token(as);
        value = expr_evaluate(as);
        expect_op(as, ')');
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Unexpected primary token");
        value = -1;
    }
    return value;
}

static int64_t expr_factor(ASSEMBLER *as) {
    if(as->token.type == TOKEN_OP) {
        if(as->token.op == '+') {
            next_token(as);
            return expr_factor(as);
        } else if(as->token.op == '-') {
            next_token(as);
            return -expr_factor(as);
        } else if(as->token.op == '<') {
            next_token(as);
            int64_t value = expr_factor(as) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->token.op == '>') {
            next_token(as);
            int64_t value = (expr_factor(as) >> 8) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->token.op == '!') {
            next_token(as);
            return !expr_factor(as);
        } else if(as->token.op == '~') {
            next_token(as);
            return ~expr_factor(as);
        }
    }
    return expr_primary(as);
}

static int64_t expr_exponentiation(ASSEMBLER *as) {
    int64_t value = expr_factor(as);
    while(as->token.type == TOKEN_OP && as->token.op == 'P') {
        next_token(as);
        int64_t right = expr_factor(as);
        value = expr_exponentiation_by_squaring(value, right);
    }
    return value;
}

static int64_t expr_term(ASSEMBLER *as) {
    int64_t value = expr_exponentiation(as);
    while(as->token.type == TOKEN_OP && (as->token.op == '*' || as->token.op == '/' || as->token.op == '%')) {
        char op = as->token.op;
        next_token(as);
        int64_t right = expr_exponentiation(as);
        if(op == '*') {
            value *= right;
        } else if(op == '/') {
            value /= right;
        } else if(op == '%') {
            value %= right;
        }
    }
    return value;
}

static int64_t expr_additive(ASSEMBLER *as) {
    int64_t value = expr_term(as);
    while(as->token.type == TOKEN_OP && (as->token.op == '+' || as->token.op == '-')) {
        char op = as->token.op;
        next_token(as);
        int64_t right = expr_term(as);
        if(op == '+') {
            value += right;
        } else {
            value -= right;
        }
    }
    return value;
}

static int64_t expr_shift(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_additive(as);
    while(as->token.type == TOKEN_OP && (as->token.op == '<' || as->token.op == '>')) {
        char op = as->token.op;
        next_token(as);
        expect_op(as, op);
        int64_t right = expr_additive(as);
        if(op == '<') {
            value <<= right;
        } else {
            value >>= right;
        }
    }
    return (int64_t)value;
}

static int64_t expr_relational(ASSEMBLER *as) {
    int64_t value = expr_shift(as);
    while(as->token.type == TOKEN_OP && (toupper((unsigned char)as->token.op) == 'L' ||
                                         toupper((unsigned char)as->token.op) == 'G')) {
        char op = as->token.op;
        next_token(as);
        int64_t right = expr_shift(as);
        if(op == 'L') {
            value = value < right;
        } else if(op == 'G') {
            value = value > right;
        } else if(op == 'l') {
            value = value <= right;
        } else if(op == 'g') {
            value = value >= right;
        }
    }
    return value;
}

static int64_t expr_equality(ASSEMBLER *as) {
    int64_t value = expr_relational(as);
    while(as->token.type == TOKEN_OP && (toupper((unsigned char)as->token.op) == 'E' ||
                                         toupper((unsigned char)as->token.op) == 'N')) {
        char op = as->token.op;
        next_token(as);
        int64_t right = expr_relational(as);
        if(op == 'E') {
            value = value == right;
        } else if(op == 'N') {
            value = value != right;
        }
    }
    return value;
}

static int64_t expr_bitwise_and(ASSEMBLER *as) {
    int64_t value = expr_equality(as);
    while(as->token.type == TOKEN_OP && as->token.op == '&') {
        next_token(as);
        value &= expr_equality(as);
    }
    return value;
}

static int64_t expr_bitwise_xor(ASSEMBLER *as) {
    int64_t value = expr_bitwise_and(as);
    while(as->token.type == TOKEN_OP && as->token.op == '^') {
        next_token(as);
        value ^= expr_bitwise_and(as);
    }
    return value;
}

static int64_t expr_bitwise_or(ASSEMBLER *as) {
    int64_t value = expr_bitwise_xor(as);
    while(as->token.type == TOKEN_OP && as->token.op == '|') {
        next_token(as);
        value |= expr_bitwise_xor(as);
    }
    return value;
}

static int64_t expr_logical(ASSEMBLER *as) {
    int64_t value = expr_bitwise_or(as);
    while(as->token.type == TOKEN_OP && (toupper((unsigned char)as->token.op) == 'A' ||
                                         toupper((unsigned char)as->token.op) == 'O')) {
        char op = as->token.op;
        next_token(as);
        int64_t right = expr_bitwise_or(as);
        if(op == 'A') {
            value = value && right;
        } else {
            value = value || right;
        }
    }
    return value;
}

static int64_t expr_conditional(ASSEMBLER *as, int64_t condition_value) {
    next_token(as);
    int64_t true_condition = expr_evaluate(as);
    expect_op(as, ':');
    int64_t false_condition = expr_evaluate(as);
    return condition_value ? true_condition : false_condition;
}

int64_t expr_evaluate(ASSEMBLER *as) {
    int root_expression = as->expression_depth == 0;
    if(root_expression) {
        as->expression_size = 0;
        as->expression_unknown = 0;
    }
    as->expression_depth++;
    int64_t value = expr_logical(as);
    if(as->token.type == TOKEN_OP && as->token.op == '?') {
        value = expr_conditional(as, value);
    }
    as->expression_depth--;
    return value;
}

int64_t expr_full_evaluate(ASSEMBLER *as) {
    next_token(as);
    int64_t result = expr_evaluate(as);
    if(as->token.type != TOKEN_END) {
        asm_err(as, ASM_ERR_RESOLVE, "Unexpected token after expression");
    }
    return result;
}

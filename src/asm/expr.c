// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Math worker function
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
    // Ensure the array has items
    if(as->anon_symbols.items == 0) {
        *address = 0xFFFF;
        return 0;                                           // Not found
    }

    int low = 0;
    int high = (int) as->anon_symbols.items - 1;
    int exact_match_index = -1;
    int closest_smaller_index = -1;
    int target_index;

    // Perform binary search to find the closest smaller or equal label
    while(low <= high) {
        int mid = (low + high) / 2;
        uint16_t value = *ARRAY_GET(&as->anon_symbols, uint16_t, mid);

        if(value == *address) {
            exact_match_index = mid;                        // Exact match found, not sure this can happen?
            break;
        } else if(value < *address) {
            closest_smaller_index = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if(exact_match_index != -1) {
        // If we have an exact match
        target_index = exact_match_index + direction;
    } else {
        // Determine the target_index with direction
        if(direction < 0) {
            // For negative direction: Start at the label greater than
            // the closest_smaller_index
            target_index = closest_smaller_index + 1 + direction;
        } else {
            // For zero or positive direction: Start at the closest_smaller_index
            target_index = closest_smaller_index + direction;
        }
    }

    // Check bounds
    if(target_index < 0 || target_index >= (int) as->anon_symbols.items) {
        // Error out of bounds
        *address = 0xFFFF;
        return 0;
    }

    // Return the address at the target index
    *address = *ARRAY_GET(&as->anon_symbols, uint16_t, target_index);
    return 1;
}

static uint16_t expr_anonymous_address(ASSEMBLER *as) {
    // Anonymous label
    next_token(as);
    char op = as->current_token.op;
    int direction = 0;
    while(op == as->current_token.op && as->current_token.type == TOKEN_OP) {
        direction++;
        next_token(as);
    }
    switch(op) {
        case '+':
            break;
        case '-':
            direction = -direction;
            break;
        default:
            asm_err(as, ASM_ERR_RESOLVE, "Unexpected symbol after anonymous : (%c)", op);
            break;
    }
    // The opcode has not been emitted so + 1
    uint16_t address = current_output_address(as) + 1;
    if(!anonymous_symbol_lookup(as, &address, direction)) {
        asm_err(as, ASM_ERR_RESOLVE, "Invalid anonymous label address");
    }
    return address;
}

//----------------------------------------------------------------------------
// Expression evaluator worker routines - These routines are called, from bottom
// upwards, recursively, to evaluate expressions
static int64_t expr_primary(ASSEMBLER *as) {
    int64_t value;
    // Expression starting with * is an address expression
    if(as->current_token.type == TOKEN_OP && as->current_token.op == '*') {
        next_token(as);
        value = 1 + current_output_address(as);
    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == ':') {
        value = expr_anonymous_address(as);
    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == 'D') {
        value = 0;
        next_token(as);
        // If there is a token, something is defined
        if(as->current_token.type != TOKEN_END) {
            // Now consume everything to the end of the line
            while(as->current_token.type != TOKEN_END || !util_character_in_characters(as->current_token.op, ";\n\r")) {
                next_token(as);
            }
            value = 1;
        } else {
            // Nothing defined
            value = 0;
        }
    } else if(as->current_token.type == TOKEN_NUM) {
        value = as->current_token.value;
        next_token(as);
    } else if(as->current_token.type == TOKEN_VAR) {
        SYMBOL_LABEL *sl;
        // This is very convenient, but not realy correct.
        // Token to use later -- detects ++ etc.
        // Take it before processing the symbol because that could could
        // early return
        next_token(as);
        sl = symbol_read(as, as->current_token.name, as->current_token.name_length);
        if(!sl || sl->symbol_type == SYMBOL_UNKNOWN || sl->symbol_type == SYMBOL_LOCAL) {
            if(as->pass == 1) {
                if(!sl) {
                    // Make a symbol so that, in future, the 16-bit-ness is remembered
                    sl = symbol_write(as, as->current_token.name, as->current_token.name_length, SYMBOL_UNKNOWN, 0xFFFF);
                }
                // Looking up an undefined makes it 16-bit - no choice
                sl->symbol_width = 16;
            } else {
                // First, see if the UNKNOWN symbol can resolve now
                if(sl) {
                    if(sl->symbol_type == SYMBOL_UNKNOWN) {
                        // Could be UNKNOWN in the parent scopes till
                        // it resolves, if it ever resolves
                        SCOPE *s = as->active_scope->parent_scope;
                        while(s) {
                            sl = symbol_lookup_chain(s, sl->symbol_hash, sl->symbol_name, sl->symbol_length);
                            if(sl && sl->symbol_type != SYMBOL_UNKNOWN) {
                                // It resolved to not UNKNOWN
                                as->expression_size = 16;
                                break;
                            }
                            s = s->parent_scope;
                        }
                        // Never found or resolved
                        if(sl && sl->symbol_type == SYMBOL_UNKNOWN) {
                            sl = NULL;
                        }
                    } else {
                        sl = NULL;
                    }
                }
                // In pass 2, if never found or resolved, it's an error
                if(!sl) {
                    // Log the error on Pass 2
                    asm_err(as, ASM_ERR_RESOLVE, "Value for %.*s not found", as->current_token.name_length, as->current_token.name);
                    // And return zero sp exressions like lda #<notfound> don't throw further 16-bit errors
                    return 0x0;
                }
            }
        }
        // There is a symbol, use its value
        value = sl->symbol_value;

        // Now process on the token taken earlier
        if(as->current_token.type == TOKEN_OP) {
            char op = as->current_token.op;
            if(op == '=') {
                next_token(as);
                if(sl->symbol_type != SYMBOL_ADDRESS) {
                    value = sl->symbol_value = expr_evaluate(as);
                    sl->symbol_type = SYMBOL_VARIABLE;
                    // if it was used unknown or is assigned from an unknown it becomes 2 byte
                    sl->symbol_width |= as->expression_size;
                } else {
                    op = 0;
                }
            } else if(op == '+' || op == '-') {
                if(op == *as->token_start) {
                    next_token(as);
                    next_token(as);
                    // SQW At this point I can know, in pass 1, that an uninitialized variable
                    // is going to get used with ++ or -- but since pass 1 errors are
                    // ignored, I can't log it :(
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
                asm_err(as, ASM_ERR_RESOLVE, "Cannot assign value to label %.*s", sl->symbol_length, sl->symbol_name);
            }
        }
        // The expression width is as wide as the widest element
        if(sl->symbol_width > as->expression_size) {
            as->expression_size = sl->symbol_width;
        }

    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == '(') {
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
    if(as->current_token.type == TOKEN_OP) {
        if(as->current_token.op == '+') {                   // Unary positive
            next_token(as);
            return expr_factor(as);
        } else if(as->current_token.op == '-') {            // Unary negation
            next_token(as);
            return -expr_factor(as);
        } else if(as->current_token.op == '<') {            // Low byte
            next_token(as);
            int64_t value = expr_factor(as) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->current_token.op == '>') {            // High byte
            next_token(as);
            int64_t value = (expr_factor(as) >> 8) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->current_token.op == '!') {            // Unary NOT
            next_token(as);
            return !expr_factor(as);
        } else if(as->current_token.op == '~') {            // Binary NOT
            next_token(as);
            return ~expr_factor(as);
        }
    }
    return expr_primary(as);                                 // Fallback to expr_primary for numbers, variables, etc.
}

static int64_t expr_exponentiation(ASSEMBLER *as) {
    int64_t value = expr_factor(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == 'P') { // '**' as 'P'ower
        next_token(as);
        int64_t right = expr_factor(as);
        value = expr_exponentiation_by_squaring(value, right);
    }
    return value;
}

static int64_t expr_term(ASSEMBLER *as) {
    int64_t value = expr_exponentiation(as);
    while(as->current_token.type == TOKEN_OP
            && (as->current_token.op == '*' || as->current_token.op == '/' || as->current_token.op == '%')) {
        char op = as->current_token.op;
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
    while(as->current_token.type == TOKEN_OP && (as->current_token.op == '+' || as->current_token.op == '-')) {
        char op = as->current_token.op;
        next_token(as);
        int64_t right = expr_term(as);
        if(op == '+') {
            value += right;
        } else if(op == '-') {
            value -= right;
        }
    }
    return value;
}

static int64_t expr_shift(ASSEMBLER *as) {
    uint64_t value = (uint64_t)expr_additive(as);
    while(as->current_token.type == TOKEN_OP && (as->current_token.op == '<' || as->current_token.op == '>')) {
        char op = as->current_token.op;
        next_token(as);
        expect_op(as, op);                                        // Expect double operators for << and >>
        int64_t right = expr_additive(as);
        if(op == '<') {
            value <<= right;
        } else if(op == '>') {
            value >>= right;
        }
    }
    return (int64_t)value;
}

static int64_t expr_relational(ASSEMBLER *as) {
    int64_t value = expr_shift(as);
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'L' || // .lt | .le
            toupper(as->current_token.op) == 'G')) { // .gt | .ge
        char op = as->current_token.op;
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
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'E' || // .eq
            toupper(as->current_token.op) == 'N')) { // .ne
        char op = as->current_token.op;
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
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '&') {
        next_token(as);
        value &= expr_equality(as);
    }
    return value;
}

static int64_t expr_bitwise_xor(ASSEMBLER *as) {
    int64_t value = expr_bitwise_and(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '^') {
        next_token(as);
        value ^= expr_bitwise_and(as);
    }
    return value;
}

static int64_t expr_bitwise_or(ASSEMBLER *as) {
    int64_t value = expr_bitwise_xor(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '|') {
        next_token(as);
        value |= expr_bitwise_xor(as);
    }
    return value;
}

static int64_t expr_logical(ASSEMBLER *as) {
    int64_t value = expr_bitwise_or(as);
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'A' || toupper(as->current_token.op) == 'O')) {
        char op = as->current_token.op;
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
    as->expression_size = 0;
    int64_t value = expr_logical(as);
    if(as->current_token.type == TOKEN_OP && as->current_token.op == '?') {
        value = expr_conditional(as, value);
    }

    return value;
}

int64_t expr_full_evaluate(ASSEMBLER *as) {
    next_token(as);
    int64_t result = expr_evaluate(as);
    if(as->current_token.type != TOKEN_END) {
        asm_err(as, ASM_ERR_RESOLVE, "Unexpected token after expression");
    }
    return result;
}

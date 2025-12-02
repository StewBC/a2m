// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Main tokenization routines
void get_token(ASSEMBLER *as) {
    // Expression separators
    const char c = util_character_in_characters(*as->input, ",;\n\r");
    if(!(*as->input) || c) {
        // End of file, or a line/token terminating character encountered
        as->current_token.type = TOKEN_END;
        as->current_token.op = c;
        if(c == ',') {
            as->input++;
        } else if(c == ';') {
            while(*as->input && !util_is_newline(*as->input)) {
                as->input++;
            }
        }
        while(util_is_newline(*as->input)) {
            as->next_line_count++;
            // MS line endings are 2 characters
            if(util_is_newline(*(as->input + 1)) && *as->input != *(as->input + 1)) {
                as->input++;
            }
            as->input++;
            as->next_line_start = as->input;
        }
        as->token_start = as->input;
        return;
    }

    if(as->next_line_count) {
        as->line_start = as->next_line_start;
        as->current_line += as->next_line_count;
        as->next_line_count = 0;
    }
    // Skip whitespace
    while(isspace(*as->input) && !util_is_newline(*as->input)) {
        as->input++;
    }

    // Detect end of expression characters
    if(*as->input == '\0' || util_character_in_characters(*as->input, ",;\n\r")) {
        get_token(as);
        return;
    }
    // include valid token characters in token
    as->token_start = as->input;
    int instring = *as->token_start == '"';
    if(instring) {
        as->input++;
    }
    while(*as->input) {
        const char c = *as->input;
        if(isalnum(c) || c == '_' || (instring && !util_character_in_characters(c, "\"\n\r"))) {
            as->input++;
        } else {
            if(instring) {
                if(c != '"') {
                    asm_err(as, "String missing a closing \"");
                } else {
                    as->input++;
                }
            } else if(c == ':') {
                // A label ends with a : so include in token - This does mean that
                // conditional expressions need a space or bracket before the ':' in
                // calls to evaluate_expression
                as->input++;
            }
            as->current_token.op = '\0';
            break;
        }
    }

    // If the token starts with an invalid character
    if(as->token_start == as->input) {
        // if it's a dot word, get the word
        if(*as->input == '.') {
            as->input++;
            // . (dot) words are just alpha characters
            // This makes it so .lt<number> works otherwise it has to be
            // .lt <number> (because the dot command will also consume the numbers)
            while(isalpha(*as->input)) {
                as->input++;
            }
        } else {
            as->input++;
        }
    }
}

void next_token(ASSEMBLER *as) {
    get_token(as);

    if(as->token_start >= as->input || util_character_in_characters(*as->token_start, ",;\n\r")) {
        as->current_token.type = TOKEN_END;
    } else if(*as->token_start == '$') {                    // Hex number
        get_token(as);
        as->current_token.type = TOKEN_NUM;
        as->current_token.value = strtoll(as->token_start, (char **) &as->token_start, 16);
        as->input = as->token_start;                        // If there are unconverted characters, set the input back to them
    } else if(*as->token_start == '0') {                    // Octal number
        as->token_start++;
        as->current_token.type = TOKEN_NUM;
        as->current_token.value = strtoll(as->token_start, (char **) &as->token_start, 8);
        as->input = as->token_start;
    } else if(*as->token_start == '%') {                    // Binary or Mod
        if(*(as->token_start + 1) == '0' || *(as->token_start + 1) == '1') {
            get_token(as);
            as->current_token.type = TOKEN_NUM;             // Binary number
            as->current_token.value = strtoll(as->token_start, (char **) &as->token_start, 2);
            as->input = as->token_start;
        } else {
            as->current_token.type = TOKEN_OP;
            as->current_token.op = '%';                     // Mod
        }
    } else if(*as->token_start == '*') {                    // Detect multiply or exponentiation '**'
        if(*(as->token_start + 1) == '*') {
            get_token(as);
            as->current_token.type = TOKEN_OP;
            as->current_token.op = 'P';                     // '**' = Raise to 'P'ower of
        } else {
            as->current_token.type = TOKEN_OP;
            as->current_token.op = '*';                     // Multiply
        }
    } else if(*as->token_start == '&') {                    // And
        if(*(as->token_start + 1) == '&') {
            get_token(as);
            as->current_token.type = TOKEN_OP;
            as->current_token.op = 'A';                     // Logical And '&&' as 'A'nd
        } else {
            as->current_token.type = TOKEN_OP;
            as->current_token.op = '&';                     // Binary And
        }
    } else if(*as->token_start == '|') {                    // Or
        if(*(as->token_start + 1) == '|') {
            get_token(as);
            as->current_token.type = TOKEN_OP;
            as->current_token.op = 'O';                     // Logical Or '||' as 'O'r
        } else {
            as->current_token.type = TOKEN_OP;
            as->current_token.op = '|';                     // Binary Or
        }
    } else if(*as->token_start == '.') {                    // .lt, .le, .gt, .ge, .ne & .eq
        as->token_start++;
        char first = toupper(*as->token_start);
        if(0 != strnicmp(as->token_start, "defined", 7)) {
            as->current_token.type = TOKEN_OP;
            as->current_token.op = first;
            as->token_start++;
            char second = toupper(*as->token_start);
            switch(first) {
                case 'L':
                case 'G':
                    if(second == 'E') {
                        as->current_token.op = tolower(first);
                    } else if(second != 'T') {
                        asm_err(as, "Expected .%cT or %cE", first, first);
                    }
                    break;
                case 'E':
                    if(second != 'Q') {
                        asm_err(as, "Expected .EQ");
                    }
                    break;
                case 'N':
                    if(second != 'E') {
                        asm_err(as, "Expected .NE");
                    }
                    break;
                default:
                    asm_err(as, "Expected .LT, .LE, .GT, .GE, .EQ or .NE");
            }
        } else {
            // 'D' is now the token for defined
            as->current_token.op = first;
            as->current_token.type = TOKEN_OP;
        }

    } else if(isdigit(*as->token_start)) {
        as->current_token.type = TOKEN_NUM;                 // Decimal Number
        as->current_token.value = strtoll(as->token_start, (char **) &as->token_start, 10);
    } else if(isalpha(*as->token_start) || *as->token_start == '_') { // Detect variable names, allowing '_'
        as->current_token.type = TOKEN_VAR;                 // Variable Name
        as->current_token.name = as->token_start;
        as->current_token.name_length = as->input - as->token_start;
        as->current_token.name_hash = util_fnv_1a_hash(as->current_token.name, as->current_token.name_length);
    } else if(*as->token_start == '\'') {
        get_token(as);
        as->current_token.type = TOKEN_NUM;
        as->current_token.value = *as->token_start;
        if(*as->input != '\'') {
            asm_err(as, "Expected a closing '");
        }
        get_token(as);
        // as->current_token.type = TOKEN_NUM;  // All other one char operators (+ - ? : etc.)
        // get_token(as);
    } else {
        as->current_token.type = TOKEN_OP;                  // All other one char operators (+ - ? : etc.)
        as->current_token.op = *as->token_start++;
    }
}

//----------------------------------------------------------------------------
// Math worker function
int64_t exponentiation_by_squaring(int64_t base, int64_t exp) {
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

//----------------------------------------------------------------------------
// Expression parser worker routines - These routines are called, from bottom
// upwards, recursively, to parse expressions
int64_t parse_primary(ASSEMBLER *as) {
    int64_t value;
    // Expression starting with * is an address expression
    if(as->current_token.type == TOKEN_OP && as->current_token.op == '*') {
        next_token(as);
        value = 1 + as->current_address;
    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == ':') {
        value = parse_anonymous_address(as);
    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == 'D') {
        value = 0;
        next_token(as);
        if(as->current_token.type == TOKEN_VAR) {
            SYMBOL_LABEL *sl = symbol_lookup(as, as->current_token.name_hash, as->current_token.name, as->current_token.name_length);
            if(sl) {
                value = 1;
            }
            next_token(as);
        } else {
            asm_err(as, ".defined expects a variable to follow");
        }
    } else if(as->current_token.type == TOKEN_NUM) {
        value = as->current_token.value;
        next_token(as);
    } else if(as->current_token.type == TOKEN_VAR) {
        SYMBOL_LABEL *sl = symbol_lookup(as, as->current_token.name_hash, as->current_token.name, as->current_token.name_length);
        if(!sl || (as->pass == 2 && sl && sl->symbol_type == SYMBOL_UNKNOWN)) {
            // All tokens must have resolved by pass 2
            if(as->pass == 2) {
                asm_err(as, "Value for %.*s not found", as->current_token.name_length, as->current_token.name);
            }
            // In pass 1 tokens not found have placeholders (SYMBOL_UNKNOWN) created
            value = 0xFFFF;
            sl = symbol_store(as, as->current_token.name, as->current_token.name_length, SYMBOL_UNKNOWN, value);
        } else {
            value = sl->symbol_value;
        }

        next_token(as);
        if(as->current_token.type == TOKEN_OP) {
            char op = as->current_token.op;
            if(op == '=') {
                next_token(as);
                if(sl->symbol_type != SYMBOL_ADDRESS) {
                    sl->symbol_value = parse_expression(as);
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
                asm_err(as, "Cannot assign value to label %.*s", sl->symbol_length, sl->symbol_name);
            }
        }
        // If a lookup reads an unknown variable, it becomes a 2-byte variable
        // regardless of value later when that becomes known
        if(sl->symbol_type == SYMBOL_UNKNOWN) {
            sl->symbol_width = 16;
        }
        // The expression width is as wide as the widest element
        if(sl->symbol_width > as->expression_size) {
            as->expression_size = sl->symbol_width;
        }

    } else if(as->current_token.type == TOKEN_OP && as->current_token.op == '(') {
        next_token(as);
        value = parse_expression(as);
        expect(as, ')');
    } else {
        asm_err(as, "Unexpected primary token");
        value = -1;
    }
    return value;
}

int64_t parse_factor(ASSEMBLER *as) {
    if(as->current_token.type == TOKEN_OP) {
        if(as->current_token.op == '+') {                   // Unary positive
            next_token(as);
            return parse_factor(as);
        } else if(as->current_token.op == '-') {            // Unary negation
            next_token(as);
            return -parse_factor(as);
        } else if(as->current_token.op == '<') {            // Low byte
            next_token(as);
            int64_t value = parse_factor(as) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->current_token.op == '>') {            // High byte
            next_token(as);
            int64_t value = (parse_factor(as) >> 8) & 0xFF;
            as->expression_size = 0;
            return value;
        } else if(as->current_token.op == '!') {            // Unary NOT
            next_token(as);
            return !parse_factor(as);
        } else if(as->current_token.op == '~') {            // Binary NOT
            next_token(as);
            return ~parse_factor(as);
        }
    }
    return parse_primary(as);                                 // Fallback to parse_primary for numbers, variables, etc.
}

int64_t parse_exponentiation(ASSEMBLER *as) {
    int64_t value = parse_factor(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == 'P') { // '**' as 'P'ower
        next_token(as);
        int64_t right = parse_factor(as);
        value = exponentiation_by_squaring(value, right);
    }
    return value;
}

int64_t parse_term(ASSEMBLER *as) {
    int64_t value = parse_exponentiation(as);
    while(as->current_token.type == TOKEN_OP
            && (as->current_token.op == '*' || as->current_token.op == '/' || as->current_token.op == '%')) {
        char op = as->current_token.op;
        next_token(as);
        int64_t right = parse_exponentiation(as);
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

int64_t parse_additive(ASSEMBLER *as) {
    int64_t value = parse_term(as);
    while(as->current_token.type == TOKEN_OP && (as->current_token.op == '+' || as->current_token.op == '-')) {
        char op = as->current_token.op;
        next_token(as);
        int64_t right = parse_term(as);
        if(op == '+') {
            value += right;
        } else if(op == '-') {
            value -= right;
        }
    }
    return value;
}

int64_t parse_shift(ASSEMBLER *as) {
    int64_t value = parse_additive(as);
    while(as->current_token.type == TOKEN_OP && (as->current_token.op == '<' || as->current_token.op == '>')) {
        char op = as->current_token.op;
        next_token(as);
        expect(as, op);                                        // Expect double operators for << and >>
        int64_t right = parse_additive(as);
        if(op == '<') {
            value <<= right;
        } else if(op == '>') {
            value >>= right;
        }
    }
    return value;
}

int64_t parse_relational(ASSEMBLER *as) {
    int64_t value = parse_shift(as);
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'L' || // .lt | .le
            toupper(as->current_token.op) == 'G')) { // .gt | .ge
        char op = as->current_token.op;
        next_token(as);
        int64_t right = parse_shift(as);
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

int64_t parse_equality(ASSEMBLER *as) {
    int64_t value = parse_relational(as);
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'E' || // .eq
            toupper(as->current_token.op) == 'N')) { // .ne
        char op = as->current_token.op;
        next_token(as);
        int64_t right = parse_relational(as);
        if(op == 'E') {
            value = value == right;
        } else if(op == 'N') {
            value = value != right;
        }
    }
    return value;
}

int64_t parse_bitwise_and(ASSEMBLER *as) {
    int64_t value = parse_equality(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '&') {
        next_token(as);
        value &= parse_equality(as);
    }
    return value;
}

int64_t parse_bitwise_xor(ASSEMBLER *as) {
    int64_t value = parse_bitwise_and(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '^') {
        next_token(as);
        value ^= parse_bitwise_and(as);
    }
    return value;
}

int64_t parse_bitwise_or(ASSEMBLER *as) {
    int64_t value = parse_bitwise_xor(as);
    while(as->current_token.type == TOKEN_OP && as->current_token.op == '|') {
        next_token(as);
        value |= parse_bitwise_xor(as);
    }
    return value;
}

int64_t parse_logical(ASSEMBLER *as) {
    int64_t value = parse_bitwise_or(as);
    while(as->current_token.type == TOKEN_OP && (toupper(as->current_token.op) == 'A' || toupper(as->current_token.op) == 'O')) {
        char op = as->current_token.op;
        next_token(as);
        int64_t right = parse_bitwise_or(as);
        if(op == 'A') {
            value = value && right;
        } else {
            value = value || right;
        }
    }
    return value;
}

int64_t parse_conditional(ASSEMBLER *as, int64_t condition_value) {
    next_token(as);
    int64_t true_condition = parse_expression(as);
    expect(as, ':');
    int64_t false_condition = parse_expression(as);
    return condition_value ? true_condition : false_condition;
}

int64_t parse_expression(ASSEMBLER *as) {
    as->expression_size = 0;
    int64_t value = parse_logical(as);
    if(as->current_token.type == TOKEN_OP && as->current_token.op == '?') {
        value = parse_conditional(as, value);
    }

    return value;
}

int64_t evaluate_expression(ASSEMBLER *as) {
    next_token(as);
    int64_t result = parse_expression(as);
    if(as->current_token.type != TOKEN_END) {
        asm_err(as, "Unexpected token after expression");
    }
    return result;
}

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
            // In strings, '\' always quotes the next character
            if(instring && c == '\\') {
                if(*(as->input + 1)) {
                    as->input++;
                }
            }
        } else {
            if(instring) {
                if(c != '"') {
                    asm_err(as, ASM_ERR_RESOLVE, "String missing a closing \"");
                } else {
                    as->input++;
                }
            } else if(c == ':') {
                // A label ends with a : so include in token and be done.
                // A scope contains :: so include that in a token and keep going
                if(':' == *(++as->input)) {
                    as->input++;
                    continue;
                }
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
            // I can here detect a number an use a separate loop so .lt<num> and .6502 will work
            // but for now it has to be .lt <num>
            while(isalnum(*as->input)) {
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
    } else if(*as->token_start == '0' && as->token_start[1] >= '0' && as->token_start[1] < '8') {                    // Octal number
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
                        asm_err(as, ASM_ERR_RESOLVE, "Expected .%cT or %cE", first, first);
                    }
                    break;
                case 'E':
                    if(second != 'Q') {
                        asm_err(as, ASM_ERR_RESOLVE, "Expected .EQ");
                    }
                    break;
                case 'N':
                    if(second != 'E') {
                        asm_err(as, ASM_ERR_RESOLVE, "Expected .NE");
                    }
                    break;
                default:
                    asm_err(as, ASM_ERR_RESOLVE, "Expected .LT, .LE, .GT, .GE, .EQ or .NE");
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
        // This is quite limiting.  Must literally be '<character>' or will error
        // No '\0' or anything else allowed as of now
        char c = as->token_start[1];
        char e = as->token_start[2];
        as->current_token.type = TOKEN_NUM;
        as->current_token.value = c;
        // Space is not a token so do not get_token to skip the space
        if(c != ' ') {
            get_token(as);
        }
        get_token(as);
        if(e != '\'' || *(as->token_start) != '\'') {
            asm_err(as, ASM_ERR_RESOLVE, "Expected a closing '");
        }
    } else if(*as->token_start == '"') {
        // token_start points at opening quote and as->input is after closing quote
        int len = (int)(as->input - as->token_start);
        if(len < 2 || as->token_start[len - 1] != '"') {
            asm_err(as, ASM_ERR_RESOLVE, "String missing a closing \"");
        }
        as->current_token.type = TOKEN_STR;
        as->current_token.name = as->token_start + 1;
        as->current_token.name_length = len - 2;
        as->current_token.name_hash = util_fnv_1a_hash(as->current_token.name, as->current_token.name_length);
    } else if(*as->token_start == ':' && *(as->token_start + 1) == ':') {
        // Scoped/root-qualified identifier like ::a or ::A::B::x
        as->current_token.type = TOKEN_VAR;
        as->current_token.name = as->token_start;
        as->current_token.name_length = as->input - as->token_start;
        as->current_token.name_hash = util_fnv_1a_hash(as->current_token.name, as->current_token.name_length);        
    } else if(*as->token_start == ':' && *(as->token_start + 1) == '=') {
        as->current_token.type = TOKEN_OP;
        as->current_token.op = '=';                         // make := equivalent to =
        as->input = as->token_start + 2;                    // consume both chars
    } else {
        as->current_token.type = TOKEN_OP;                  // All other one char operators (+ - ? : etc.)
        as->current_token.op = *as->token_start++;
    }
}

int peek_next_op(ASSEMBLER *as, int *out_op) {
    if(A2_OK != input_stack_push(as)) {
        return 0;
    }
    next_token(as);
    *out_op = as->current_token.op;
    input_stack_pop(as);
    return 1;
}

void expect_op(ASSEMBLER *as, char op) {
    if(as->current_token.type != TOKEN_OP || as->current_token.op != op) {
        asm_err(as, ASM_ERR_RESOLVE, "Expected '%c'", op);
    }
    next_token(as);
}

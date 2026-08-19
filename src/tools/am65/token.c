// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#include <ctype.h>

static uint32_t token_hash(const char *s, size_t len) {
    uint32_t hash = 2166136261u;
    for(size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)s[i];
        hash *= 16777619u;
    }
    return hash;
}

static int ascii_case_equal(const char *a, const char *b, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_identifier_start(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_identifier_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void token_clear(TOKEN *token) {
    memset(token, 0, sizeof(*token));
    token->type = TOKEN_END;
}

static void token_set_op(ASSEMBLER *as, const char *start, const char *end, char op) {
    as->token.type = TOKEN_OP;
    as->token.op = op;
    as->token.name = start;
    as->token.name_length = (uint32_t)(end - start);
    as->token.name_hash = token_hash(start, (size_t)(end - start));
    as->cur = end;
}

static void token_set_var(ASSEMBLER *as, const char *start, const char *end) {
    as->token.type = TOKEN_VAR;
    as->token.op = '\0';
    as->token.name = start;
    as->token.name_length = (uint32_t)(end - start);
    as->token.name_hash = token_hash(start, (size_t)(end - start));
    as->cur = end;
}

static void parse_dot_token(ASSEMBLER *as, const char *start) {
    const char *p = start + 1;
    while(isalnum((unsigned char)*p) || *p == '_') {
        p++;
    }

    size_t len = (size_t)(p - start - 1);
    const char *word = start + 1;
    if(len == 2) {
        char first = (char)toupper((unsigned char)word[0]);
        char second = (char)toupper((unsigned char)word[1]);
        switch(first) {
        case 'L':
            if(second == 'T') {
                token_set_op(as, start, p, 'L');
                return;
            }
            if(second == 'E') {
                token_set_op(as, start, p, 'l');
                return;
            }
            break;
        case 'G':
            if(second == 'T') {
                token_set_op(as, start, p, 'G');
                return;
            }
            if(second == 'E') {
                token_set_op(as, start, p, 'g');
                return;
            }
            break;
        case 'E':
            if(second == 'Q') {
                token_set_op(as, start, p, 'E');
                return;
            }
            break;
        case 'N':
            if(second == 'E') {
                token_set_op(as, start, p, 'N');
                return;
            }
            break;
        default:
            break;
        }
    } else if(len == 7 && ascii_case_equal(word, "defined", 7)) {
        token_set_op(as, start, p, 'D');
        return;
    }

    token_set_var(as, start, p);
}

void get_token(ASSEMBLER *as) {
    if(!as || !as->cur) {
        return;
    }
    token_clear(&as->token);

    const char *p = as->cur;
    while(*p && isspace((unsigned char)*p)) {
        p++;
    }

    const char *start = p;
    if(*p == '\0') {
        as->token.type = TOKEN_END;
        as->token.op = '\0';
        as->token.name = p;
        as->cur = p;
        return;
    }
    if(*p == ',') {
        as->token.type = TOKEN_END;
        as->token.op = ',';
        as->token.name = p;
        as->token.name_length = 1;
        as->cur = p + 1;
        return;
    }

    if(*p == '$') {
        char *end = NULL;
        as->token.type = TOKEN_NUM;
        as->token.name = p;
        as->token.value = strtoll(p + 1, &end, 16);
        as->token.name_length = (uint32_t)(end - p);
        as->cur = end;
        return;
    }

    if(*p == '%' && (p[1] == '0' || p[1] == '1')) {
        char *end = NULL;
        as->token.type = TOKEN_NUM;
        as->token.name = p;
        as->token.value = strtoll(p + 1, &end, 2);
        as->token.name_length = (uint32_t)(end - p);
        as->cur = end;
        return;
    }

    if(*p == '0' && p[1] >= '0' && p[1] < '8') {
        char *end = NULL;
        as->token.type = TOKEN_NUM;
        as->token.name = p;
        as->token.value = strtoll(p + 1, &end, 8);
        as->token.name_length = (uint32_t)(end - p);
        as->cur = end;
        return;
    }

    if(isdigit((unsigned char)*p)) {
        char *end = NULL;
        as->token.type = TOKEN_NUM;
        as->token.name = p;
        as->token.value = strtoll(p, &end, 10);
        as->token.name_length = (uint32_t)(end - p);
        as->cur = end;
        return;
    }

    if(*p == '\'') {
        as->token.type = TOKEN_NUM;
        as->token.name = p;
        if(p[1] == '\\' && p[2] && p[3] == '\'') {
            as->token.value = (uint8_t)p[2];
            as->token.name_length = 4;
            as->cur = p + 4;
        } else if(p[1] && p[2] == '\'') {
            as->token.value = (uint8_t)p[1];
            as->token.name_length = 3;
            as->cur = p + 3;
        } else {
            as->token.value = p[1] ? (uint8_t)p[1] : 0;
            as->token.name_length = p[1] ? 2 : 1;
            as->cur = p + as->token.name_length;
            asm_err(as, ASM_ERR_RESOLVE, "Expected a closing '");
        }
        return;
    }

    if(*p == '"') {
        p++;
        while(*p && *p != '"') {
            if(*p == '\\' && p[1]) {
                p += 2;
            } else {
                p++;
            }
        }
        if(*p == '"') {
            p++;
        } else {
            asm_err(as, ASM_ERR_RESOLVE, "String missing a closing \"");
        }
        as->token.type = TOKEN_STR;
        as->token.op = '\0';
        as->token.name = start + 1;
        const char *string_end = p;
        if(string_end > start + 1 && string_end[-1] == '"') {
            string_end--;
        }
        as->token.name_length = (uint32_t)(string_end - (start + 1));
        as->token.name_hash = token_hash(as->token.name, as->token.name_length);
        as->cur = p;
        return;
    }

    if(is_identifier_start(*p) || (*p == ':' && p[1] == ':')) {
        if(*p == ':' && p[1] == ':') {
            p += 2;
        } else {
            p++;
        }
        while(*p) {
            if(is_identifier_char(*p)) {
                p++;
            } else if(*p == ':' && p[1] == ':') {
                p += 2;
            } else {
                break;
            }
        }
        token_set_var(as, start, p);
        return;
    }

    if(*p == '.') {
        parse_dot_token(as, p);
        return;
    }

    if(*p == ':' && p[1] == '=') {
        token_set_op(as, p, p + 2, '=');
        return;
    }

    if(*p == '*' && p[1] == '*') {
        token_set_op(as, p, p + 2, 'P');
        return;
    }
    if(*p == '&' && p[1] == '&') {
        token_set_op(as, p, p + 2, 'A');
        return;
    }
    if(*p == '|' && p[1] == '|') {
        token_set_op(as, p, p + 2, 'O');
        return;
    }

    token_set_op(as, p, p + 1, *p);
}

void next_token(ASSEMBLER *as) {
    get_token(as);
}

int peek_next_op(ASSEMBLER *as, int *out_op) {
    if(!as || !out_op) {
        return 0;
    }

    const char *saved_cur = as->cur;
    TOKEN saved_token = as->token;
    get_token(as);
    *out_op = as->token.op;
    as->cur = saved_cur;
    as->token = saved_token;
    return 1;
}

void expect_op(ASSEMBLER *as, char op) {
    if(as->token.type != TOKEN_OP || as->token.op != op) {
        asm_err(as, ASM_ERR_RESOLVE, "Expected '%c'", op);
    } else {
        get_token(as);
    }
}

// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#include <ctype.h>

#define DEFINE_SUBSTITUTE_LIMIT 64

static char *define_strndup(const char *s, int len) {
    if(!s || len < 0) {
        return NULL;
    }
    char *out = malloc((size_t)len + 1);
    if(!out) {
        return NULL;
    }
    memcpy(out, s, (size_t)len);
    out[len] = '\0';
    return out;
}

static int is_identifier_start(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_identifier_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int define_index_for_from(ASSEMBLER *as, const char *from, int from_len) {
    for(size_t i = 0; i < as->defines.items; i++) {
        DEFINE *d = AM65_ARRAY_GET(&as->defines, DEFINE, i);
        if(d->from_len == from_len && 0 == memcmp(d->from, from, (size_t)from_len)) {
            return (int)i;
        }
    }
    return -1;
}

static int define_matches_at(ASSEMBLER *as, DEFINE *d, int pos) {
    if(d->from_len <= 0 || pos + d->from_len > as->line_len) {
        return 0;
    }
    if(0 != memcmp(as->line + pos, d->from, (size_t)d->from_len)) {
        return 0;
    }

    if(is_identifier_start((unsigned char)d->from[0])) {
        if(pos > 0 && is_identifier_char((unsigned char)as->line[pos - 1])) {
            return 0;
        }
        if(pos + d->from_len < as->line_len &&
           is_identifier_char((unsigned char)as->line[pos + d->from_len])) {
            return 0;
        }
    }
    return 1;
}

static void define_replace_at(ASSEMBLER *as, DEFINE *d, int pos) {
    int tail_pos = pos + d->from_len;
    int tail_len = as->line_len - tail_pos;
    int new_len = as->line_len - d->from_len + d->to_len;

    if(new_len >= ASM_MAX_LINE) {
        asm_err(as, ASM_ERR_FATAL, "Line too long after .define substitution; truncated");
        new_len = ASM_MAX_LINE - 1;
    }

    int available_to_len = d->to_len;
    if(pos + available_to_len > new_len) {
        available_to_len = new_len - pos;
    }
    if(available_to_len < 0) {
        available_to_len = 0;
    }

    int new_tail_pos = pos + d->to_len;
    int max_tail_len = new_len - new_tail_pos;
    if(max_tail_len < 0) {
        max_tail_len = 0;
    }
    if(tail_len > max_tail_len) {
        tail_len = max_tail_len;
    }

    if(tail_len > 0) {
        memmove(as->line + new_tail_pos, as->line + tail_pos, (size_t)tail_len);
    }
    if(available_to_len > 0) {
        memcpy(as->line + pos, d->to, (size_t)available_to_len);
    }
    as->line_len = new_len;
    as->line[as->line_len] = '\0';
}

void define_add(ASSEMBLER *as, const char *from, int from_len, const char *to, int to_len) {
    if(!as || !from || from_len <= 0 || !to || to_len < 0) {
        return;
    }

    char *from_copy = define_strndup(from, from_len);
    char *to_copy = define_strndup(to, to_len);
    if(!from_copy || !to_copy) {
        free(from_copy);
        free(to_copy);
        asm_err(as, ASM_ERR_FATAL, "Out of memory adding .define");
        return;
    }

    int existing = define_index_for_from(as, from, from_len);
    if(existing >= 0) {
        DEFINE *d = AM65_ARRAY_GET(&as->defines, DEFINE, (size_t)existing);
        free(d->from);
        free(d->to);
        d->from = from_copy;
        d->from_len = from_len;
        d->to = to_copy;
        d->to_len = to_len;
        return;
    }

    DEFINE d;
    d.from = from_copy;
    d.from_len = from_len;
    d.to = to_copy;
    d.to_len = to_len;

    if(ASM_OK != AM65_ARRAY_ADD(&as->defines, d)) {
        free(from_copy);
        free(to_copy);
        asm_err(as, ASM_ERR_FATAL, "Out of memory adding .define");
    }
}

void define_substitute(ASSEMBLER *as) {
    if(!as || as->defines.items == 0 || as->line_len <= 0) {
        return;
    }

    int substitutions = 0;
    for(int pos = 0; pos < as->line_len;) {
        if(as->line[pos] == '"') {
            pos++;
            while(pos < as->line_len) {
                if(as->line[pos] == '\\' && pos + 1 < as->line_len) {
                    pos += 2;
                } else if(as->line[pos] == '"') {
                    pos++;
                    break;
                } else {
                    pos++;
                }
            }
            continue;
        }

        DEFINE *matched = NULL;
        for(size_t i = 0; i < as->defines.items; i++) {
            DEFINE *d = AM65_ARRAY_GET(&as->defines, DEFINE, i);
            if(define_matches_at(as, d, pos)) {
                matched = d;
                break;
            }
        }

        if(matched) {
            define_replace_at(as, matched, pos);
            substitutions++;
            if(substitutions >= DEFINE_SUBSTITUTE_LIMIT) {
                asm_err(as, ASM_ERR_FATAL, ".define substitution limit reached; possible cycle");
                return;
            }
            continue;
        }
        pos++;
    }
}

void defines_free(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->defines.items; i++) {
        DEFINE *d = AM65_ARRAY_GET(&as->defines, DEFINE, i);
        free(d->from);
        free(d->to);
    }
    am65_array_free(&as->defines);
    AM65_ARRAY_INIT(&as->defines, DEFINE);
}

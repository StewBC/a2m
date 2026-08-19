// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

typedef struct {
    SCOPE *scope;
    const char *name;
    int name_length;
} QUALIFIED_REF;

typedef enum {
    QRES_OK = 0,
    QRES_NO_SUCH_SCOPE,
    QRES_MALFORMED
} QRES;

typedef struct {
    SCOPE *scope;
    const char *name;
    uint32_t name_length;
    uint32_t name_hash;
    int is_qualified;
} SYMREF;

static QRES symbol_resolve_qualified_name(ASSEMBLER *as, const char *sym, int sym_len, QUALIFIED_REF *out) {
    int i = 0;

    if(sym_len <= 0) {
        return QRES_MALFORMED;
    }

    SCOPE *anchor = NULL;
    if(sym_len >= 2 && sym[0] == ':' && sym[1] == ':') {
        anchor = as->root_scope;
        i = 2;
        if(i >= sym_len) {
            return QRES_MALFORMED;
        }
    } else {
        anchor = as->active_scope;
    }

    int seg_start = i;
    int next_sep = -1;
    for(int j = i; j + 1 < sym_len; j++) {
        if(sym[j] == ':' && sym[j + 1] == ':') {
            next_sep = j;
            break;
        }
    }

    if(next_sep < 0) {
        out->scope = anchor;
        out->name = sym + i;
        out->name_length = sym_len - i;
        return QRES_OK;
    }

    const char *first = sym + seg_start;
    int first_len = next_sep - seg_start;
    if(first_len <= 0) {
        return QRES_MALFORMED;
    }

    SCOPE *cur = NULL;
    if(anchor == as->root_scope) {
        cur = scope_find_child(anchor, first, first_len);
    } else {
        SCOPE *s = anchor;
        while(s) {
            cur = scope_find_child(s, first, first_len);
            if(cur) {
                break;
            }
            s = s->parent_scope;
        }
    }

    if(!cur) {
        return QRES_NO_SUCH_SCOPE;
    }

    i = next_sep + 2;
    while(1) {
        next_sep = -1;
        for(int j = i; j + 1 < sym_len; j++) {
            if(sym[j] == ':' && sym[j + 1] == ':') {
                next_sep = j;
                break;
            }
        }

        if(next_sep < 0) {
            int name_len = sym_len - i;
            if(name_len <= 0) {
                return QRES_MALFORMED;
            }
            out->scope = cur;
            out->name = sym + i;
            out->name_length = name_len;
            return QRES_OK;
        }

        int seg_len = next_sep - i;
        if(seg_len <= 0) {
            return QRES_MALFORMED;
        }

        SCOPE *child = scope_find_child(cur, sym + i, seg_len);
        if(!child) {
            return QRES_NO_SUCH_SCOPE;
        }

        cur = child;
        i = next_sep + 2;
        if(i >= sym_len) {
            return QRES_MALFORMED;
        }
    }
}

static int symbol_resolve_ref(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMREF *out) {
    memset(out, 0, sizeof(*out));
    if(sym_len == 0) {
        return 0;
    }

    if(symbol_has_scope_path(sym, (int)sym_len)) {
        QUALIFIED_REF qr;
        QRES r = symbol_resolve_qualified_name(as, sym, (int)sym_len, &qr);
        if(r != QRES_OK) {
            return 0;
        }
        out->is_qualified = 1;
        out->scope = qr.scope;
        out->name = qr.name;
        out->name_length = (uint32_t)qr.name_length;
        out->name_hash = asm_fnv_1a_hash(out->name, out->name_length);
        return 1;
    }

    out->is_qualified = 0;
    out->scope = NULL;
    out->name = sym;
    out->name_length = sym_len;
    out->name_hash = asm_fnv_1a_hash(sym, sym_len);
    return 1;
}

static SYMBOL_LABEL *symbol_lookup_scope(SCOPE *scope, uint32_t name_hash, const char *name, uint32_t len) {
    uint8_t bucket = name_hash & HASH_MASK;
    AM65_DYNARRAY *bucket_array = &scope->symbol_table[bucket];
    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = AM65_ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && sl->symbol_length == len && !asm_strnicmp(name, sl->symbol_name, len)) {
            return sl;
        }
    }
    return NULL;
}

SYMBOL_LABEL *symbol_lookup_chain(SCOPE *s, uint32_t name_hash, const char *name, uint32_t len) {
    for(; s; s = s->parent_scope) {
        SYMBOL_LABEL *hit = symbol_lookup_scope(s, name_hash, name, len);
        if(hit) {
            return hit;
        }
    }
    return NULL;
}

int symbol_has_scope_path(const char *p, int len) {
    for(int i = 0; i + 1 < len; i++) {
        if(p[i] == ':' && p[i + 1] == ':') {
            return 1;
        }
    }
    return 0;
}

SYMBOL_LABEL *symbol_read(ASSEMBLER *as, const char *sym, uint32_t sym_len) {
    SYMREF ref;
    if(!symbol_resolve_ref(as, sym, sym_len, &ref)) {
        return NULL;
    }

    if(ref.is_qualified) {
        return symbol_lookup_scope(ref.scope, ref.name_hash, ref.name, ref.name_length);
    }

    return symbol_lookup_chain(as->active_scope, ref.name_hash, ref.name, ref.name_length);
}

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    uint32_t name_hash = asm_fnv_1a_hash(symbol_name, symbol_name_length);
    SYMBOL_LABEL *sl = symbol_lookup_scope(scope, name_hash, symbol_name, symbol_name_length);
    if(sl) {
        if(symbol_type != SYMBOL_LOCAL) {
            if(sl->symbol_type == SYMBOL_LOCAL) {
                sl->symbol_value = value;
                sl->symbol_type = symbol_type;
            } else if(sl->symbol_type == SYMBOL_UNKNOWN) {
                sl->symbol_type = symbol_type;
                sl->symbol_value = value;
            } else {
                if(sl->symbol_type != symbol_type) {
                    asm_err(as, ASM_ERR_RESOLVE, "Symbol %.*s can't be address and variable type", symbol_name_length, symbol_name);
                } else {
                    if(sl->symbol_type == SYMBOL_VARIABLE) {
                        sl->symbol_value = value;
                    } else if(sl->symbol_value != value) {
                        asm_err(as, ASM_ERR_RESOLVE, "Multiple address labels have name %.*s", symbol_name_length, symbol_name);
                    }
                }
            }
        }
    } else {
        char *owned_name = NULL;
        if(!set_name(&owned_name, symbol_name, (int)symbol_name_length)) {
            asm_err(as, ASM_ERR_FATAL, "Out of memory storing symbol %.*s", symbol_name_length, symbol_name);
            return NULL;
        }

        SYMBOL_LABEL new_sl;
        new_sl.symbol_type = symbol_type;
        new_sl.symbol_hash = name_hash;
        new_sl.symbol_length = symbol_name_length;
        new_sl.symbol_name = owned_name;
        new_sl.symbol_value = value;
        new_sl.symbol_width = 0;
        uint8_t bucket = name_hash & HASH_MASK;
        AM65_DYNARRAY *bucket_array = &scope->symbol_table[bucket];
        if(ASM_OK != AM65_ARRAY_ADD(bucket_array, new_sl)) {
            free(owned_name);
            asm_err(as, ASM_ERR_FATAL, "Out of memory storing symbol %.*s", symbol_name_length, symbol_name);
            return NULL;
        }
        sl = AM65_ARRAY_GET(bucket_array, SYMBOL_LABEL, bucket_array->items - 1);
    }
    return sl;
}

SYMBOL_LABEL *symbol_write(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMBOL_TYPE symbol_type, uint64_t value) {
    SYMREF ref;
    if(!symbol_resolve_ref(as, sym, sym_len, &ref)) {
        return NULL;
    }

    if(ref.is_qualified) {
        return symbol_store_in_scope(as, ref.scope, ref.name, ref.name_length, symbol_type, value);
    }

    return symbol_store_in_scope(as, as->active_scope, ref.name, ref.name_length, symbol_type, value);
}

SYMBOL_LABEL *symbol_declare_local(ASSEMBLER *as, const char *name, uint32_t name_len) {
    if(as->macro_stack.items == 0) {
        return NULL;
    }

    RENAME_MAP ren;
    ren.user_name = malloc((size_t)name_len + 1);
    if(!ren.user_name) {
        return NULL;
    }
    memcpy(ren.user_name, name, name_len);
    ren.user_name[name_len] = '\0';
    ren.user_name_len = name_len;
    ren.generated_name = malloc(GEN_NAME_LEN + 1);
    if(!ren.generated_name) {
        free(ren.user_name);
        return NULL;
    }

    MACRO_EXPAND *mes = AM65_ARRAY_GET(&as->macro_stack, MACRO_EXPAND, as->macro_stack.items - 1);
    snprintf(ren.generated_name, GEN_NAME_LEN + 1, GEN_NAME_FMT, ++as->macro_id);
    if(ASM_OK != AM65_ARRAY_ADD(&mes->renames, ren)) {
        free(ren.user_name);
        free(ren.generated_name);
        return NULL;
    }
    return symbol_store_in_scope(as, as->active_scope, ren.generated_name, GEN_NAME_LEN, SYMBOL_LOCAL, 0);
}

int symbol_delete_local(SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, int force) {
    uint32_t name_hash = asm_fnv_1a_hash(symbol_name, symbol_name_length);
    uint8_t bucket = name_hash & HASH_MASK;
    AM65_DYNARRAY *bucket_array = &scope->symbol_table[bucket];

    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = AM65_ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && sl->symbol_length == symbol_name_length && !asm_strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
            if(!force && sl->symbol_type == SYMBOL_ADDRESS) {
                return 0;
            }
            size_t last = bucket_array->items - 1;
            if(i != last) {
                SYMBOL_LABEL *dst = AM65_ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
                SYMBOL_LABEL *src = AM65_ARRAY_GET(bucket_array, SYMBOL_LABEL, last);
                *dst = *src;
            }
            bucket_array->items--;
            return 1;
        }
    }
    return 0;
}

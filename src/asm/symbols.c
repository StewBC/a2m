// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// Symbol storage / lookup
static QRES symbol_resolve_qualified_name(ASSEMBLER *as, const char *sym, int sym_len, QUALIFIED_REF *out) {
    // sym is not null-terminated
    int i = 0;

    // Reject empty
    if(sym_len <= 0) {
        return QRES_MALFORMED;
    }

    // Determine anchor
    SCOPE *anchor = NULL;
    if(sym_len >= 2 && sym[0] == ':' && sym[1] == ':') {
        // Root anchored
        anchor = as->root_scope;
        i = 2;
        if(i >= sym_len) {
            return QRES_MALFORMED;        // "::" alone is not a valid symbol
        }
    } else {
        // Lexical anchor (first segment can be found up the parent chain)
        anchor = as->active_scope;
    }

    // Parse segments separated by "::".
    // The last segment is the symbol name. Everything before it is a scope path

    // Helper to find next "::" starting at i
    int seg_start = i;
    int next_sep = -1;
    for(int j = i; j + 1 < sym_len; j++) {
        if(sym[j] == ':' && sym[j + 1] == ':') {
            next_sep = j;
            break;
        }
    }

    // No separator - treat as simple 
    // This should not happen since caller (symbol lookup) should have checked
    if(next_sep < 0) {
        out->scope = anchor;
        out->name = sym + i;
        out->name_length = sym_len - i;
        return QRES_OK;
    }

    // First segment is now sym[seg_start..next_sep)
    const char *first = sym + seg_start;
    int first_len = next_sep - seg_start;
    if(first_len <= 0) {
        return QRES_MALFORMED;
    }

    // Resolve first scope name
    SCOPE *cur = NULL;

    if(anchor == as->root_scope) {
        // root-anchored: first must be child of root
        cur = scope_find_child(anchor, first, first_len);
    } else {
        // lexical - walk up parents looking for child named first
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

    // Now descend for remaining scope segments, leaving last as symbol name.
    i = next_sep + 2;

    while(1) {
        // Find next sep
        next_sep = -1;
        for(int j = i; j + 1 < sym_len; j++) {
            if(sym[j] == ':' && sym[j + 1] == ':') {
                next_sep = j;
                break;
            }
        }

        if(next_sep < 0) {
            // No more seps: remainder is final symbol name
            int name_len = sym_len - i;
            if(name_len <= 0) {
                return QRES_MALFORMED;
            }
            out->scope = cur;
            out->name = sym + i;
            out->name_length = name_len;
            return QRES_OK;
        }

        // There is another "::" - that segment is another scope name
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

static SYMBOL_LABEL *symbol_lookup_in_scope(SCOPE *scope, uint32_t name_hash, const char *name, uint32_t len) {
    uint8_t bucket = name_hash & HASH_MASK;
    DYNARRAY *bucket_array = &scope->symbol_table[bucket];
    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && sl->symbol_length == len && !strnicmp(name, sl->symbol_name, len)) {
            return sl;
        }
    }
    return NULL;
}

SYMBOL_LABEL *symbol_lookup(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    if(!token_has_scope_path(symbol_name, symbol_name_length)) {
        SCOPE *active_scope = as->active_scope;
        uint8_t bucket = name_hash & HASH_MASK;
        do {
            DYNARRAY *bucket_array = &active_scope->symbol_table[bucket];
            for(size_t i = 0; i < bucket_array->items; i++) {
                SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
                if(sl->symbol_hash == name_hash && sl->symbol_length == symbol_name_length && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
                    return sl;
                }
            }
            active_scope = active_scope->parent_scope;
        } while(active_scope);

        return NULL;
    } 
    QUALIFIED_REF qr;
    QRES r = symbol_resolve_qualified_name(as, symbol_name, (int)symbol_name_length, &qr);
    if(r != QRES_OK){
        return NULL; // treat as unresolved (pass2 error still happens)
    }

    name_hash = util_fnv_1a_hash(qr.name, (uint32_t)qr.name_length);
    return symbol_lookup_in_scope(qr.scope, name_hash, qr.name, (uint32_t)qr.name_length);
}

SYMBOL_LABEL *symbol_lookup_parent_chain(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    SCOPE *active_scope = as->active_scope ? as->active_scope->parent_scope : NULL;
    uint8_t bucket = name_hash & HASH_MASK;

    while(active_scope) {
        DYNARRAY *bucket_array = &active_scope->symbol_table[bucket];
        for(size_t i = 0; i < bucket_array->items; i++) {
            SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
            if(sl->symbol_hash == name_hash && sl->symbol_length == symbol_name_length && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
                return sl;
            }
        }
        active_scope = active_scope->parent_scope;
    }
    return NULL;
}

SYMBOL_LABEL *symbol_lookup_local(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    SCOPE *active_scope = as->active_scope;
    uint8_t bucket = name_hash & HASH_MASK;
    DYNARRAY *bucket_array = &active_scope->symbol_table[bucket];
    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && sl->symbol_length == symbol_name_length && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
            return sl;
        }
    }

    return NULL;
}

int symbol_delete_local(ASSEMBLER *as, uint32_t name_hash, const char *symbol_name, uint32_t symbol_name_length) {
    SCOPE *active_scope = as->active_scope;
    uint8_t bucket = name_hash & HASH_MASK;
    DYNARRAY *bucket_array = &active_scope->symbol_table[bucket];

    for(size_t i = 0; i < bucket_array->items; i++) {
        SYMBOL_LABEL *sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
        if(sl->symbol_hash == name_hash && sl->symbol_length == symbol_name_length && !strnicmp(symbol_name, sl->symbol_name, symbol_name_length)) {
            size_t last = bucket_array->items - 1;
            if(i != last) {
                // If the symbol to delete is in the middle, put the last symbol in the list
                // over the one to delete
                SYMBOL_LABEL *dst = ARRAY_GET(bucket_array, SYMBOL_LABEL, i);
                SYMBOL_LABEL *src = ARRAY_GET(bucket_array, SYMBOL_LABEL, last);
                *dst = *src;
            }
            bucket_array->items--;
            return 1;
        }
    }
    return 0;
}

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    uint32_t name_hash = util_fnv_1a_hash(symbol_name, symbol_name_length);
    SYMBOL_LABEL *sl = symbol_lookup_in_scope(scope, name_hash, symbol_name, symbol_name_length);
    if(sl) {
        if(sl->symbol_type == SYMBOL_UNKNOWN) {
            sl->symbol_type = symbol_type;
            sl->symbol_value = value;
        } else {
            if(sl->symbol_type != symbol_type) {
                // Symbol changing type error
                asm_err(as, ASM_ERR_RESOLVE, "Symbol %.*s can't be address and variable type", symbol_name_length, symbol_name);
            } else {
                if(sl->symbol_type == SYMBOL_VARIABLE) {
                    // Variables can change value along the way
                    sl->symbol_value = value;
                } else if(sl->symbol_value != value) {
                    // Addresses may not change value
                    asm_err(as, ASM_ERR_RESOLVE, "Multiple address labels have name %.*s", symbol_name_length, symbol_name);
                }
            }
        }
    } else {
        // Create a new entry for a previously unknown symbol
        SYMBOL_LABEL new_sl;
        new_sl.symbol_type = symbol_type;
        new_sl.symbol_hash = name_hash;
        new_sl.symbol_length = symbol_name_length;
        new_sl.symbol_name = symbol_name;
        new_sl.symbol_value = value;
        new_sl.symbol_width = 0;
        uint8_t bucket = name_hash & HASH_MASK;
        DYNARRAY *bucket_array = &scope->symbol_table[bucket];
        ARRAY_ADD(bucket_array, new_sl);
        sl = ARRAY_GET(bucket_array, SYMBOL_LABEL, bucket_array->items - 1);
    }
    return sl;
}

SYMBOL_LABEL *symbol_store_qualified(ASSEMBLER *as, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    QUALIFIED_REF qr;
    QRES r = symbol_resolve_qualified_name(as, symbol_name, symbol_name_length, &qr);
    if(r != QRES_OK){
        return NULL; // treat as unresolved (pass2 error still happens)
    }
    return symbol_store_in_scope(as, qr.scope, qr.name, (uint32_t)qr.name_length, symbol_type, value);
}


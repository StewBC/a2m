// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

typedef struct {
    SCOPE *scope;          // scope to search for the final symbol
    const char *name;      // final symbol name ptr (name portion offset in token)
    int name_length;       // final symbol name length
} QUALIFIED_REF;

typedef enum {
    QRES_OK = 0,
    QRES_NO_SUCH_SCOPE,
    QRES_MALFORMED
} QRES;

// If qualified, scope is set and name points to final segment.
// If unqualified, scope is NULL and name is the original token slice.
typedef struct {
    SCOPE *scope;
    const char *name;
    uint32_t name_length;
    uint32_t name_hash;
    int is_qualified;
} SYMREF;

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
        anchor = as->active_outer_scope;
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

static int symbol_resolve_ref(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMREF *out) {
    memset(out, 0, sizeof(*out));
    if(sym_len == 0) {
        return 0;
    }

    if(symbol_has_scope_path(sym, (int)sym_len)) {
        QUALIFIED_REF qr;
        QRES r = symbol_resolve_qualified_name(as, sym, (int)sym_len, &qr);
        if(r != QRES_OK) {
            return 0; // unresolved scope path
        }
        out->is_qualified = 1;
        out->scope = qr.scope;
        out->name = qr.name;
        out->name_length = (uint32_t)qr.name_length;
        out->name_hash = util_fnv_1a_hash(out->name, out->name_length);
        return 1;
    }

    out->is_qualified = 0;
    out->scope = NULL;
    out->name = sym;
    out->name_length = sym_len;
    out->name_hash = util_fnv_1a_hash(sym, sym_len);
    return 1;
}

static SYMBOL_LABEL *symbol_lookup_scope(SCOPE *scope, uint32_t name_hash, const char *name, uint32_t len) {
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

static SYMBOL_LABEL *symbol_lookup_chain(SCOPE *s, uint32_t name_hash, const char *name, uint32_t len) {
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

    SYMBOL_LABEL *sl = symbol_lookup_chain(as->active_locals_scope, ref.name_hash, ref.name, ref.name_length);
    if(sl) {
        return sl;
    }
    return symbol_lookup_chain(as->active_outer_scope, ref.name_hash, ref.name, ref.name_length);
}

SYMBOL_LABEL *symbol_store_in_scope(ASSEMBLER *as, SCOPE *scope, const char *symbol_name, uint32_t symbol_name_length, SYMBOL_TYPE symbol_type, uint64_t value) {
    uint32_t name_hash = util_fnv_1a_hash(symbol_name, symbol_name_length);
    SYMBOL_LABEL *sl = symbol_lookup_scope(scope, name_hash, symbol_name, symbol_name_length);
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

SYMBOL_LABEL *symbol_write(ASSEMBLER *as, const char *sym, uint32_t sym_len, SYMBOL_TYPE symbol_type, uint64_t value) {
    SYMREF ref;
    if(!symbol_resolve_ref(as, sym, sym_len, &ref)) {
        return NULL;
    }

    if(ref.is_qualified) {
        return symbol_store_in_scope(as, ref.scope, ref.name, ref.name_length, symbol_type, value);
    }

    if(symbol_type == SYMBOL_VARIABLE) {
        // first see if this symbol lives in the local scope
        for(SCOPE *ls = as->active_locals_scope; ls; ls = ls->parent_scope) {
            SYMBOL_LABEL *sl = symbol_lookup_scope(ls, ref.name_hash, ref.name, ref.name_length);
            if(sl) {
                // It is local, set it
                return symbol_store_in_scope(as, ls, ref.name, ref.name_length, symbol_type, value);
            }
        }

        // Not a local variable -> write into current proc scope (your policy)
        return symbol_store_in_scope(as, as->active_outer_scope, ref.name, ref.name_length, symbol_type, value);
    }

    // Addresses / labels: default to proc scope
    return symbol_store_in_scope(as, as->active_outer_scope, ref.name, ref.name_length, symbol_type, value);
}

// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

//----------------------------------------------------------------------------
// String Pool helpers -- there is no string pool but there probably should be
char *set_name(char **s, const char *name, const int name_length) {
    *s = (char *)malloc(name_length);
    if(*s) {
        memcpy(*s, name, name_length);
    }
    return *s;
}

//----------------------------------------------------------------------------
// SCOPE helpers
int token_has_scope_path(const char *p, int len) {
    for(int i = 0; i + 1 < len; i++) {
        if(p[i] == ':' && p[i + 1] == ':') {
            return 1;
        }
    }
    return 0;
}

void scope_to_scope(ASSEMBLER *as, SCOPE *s) {
    as->active_scope = s;
    as->symbol_table = s->symbol_table;
}

void scope_push(ASSEMBLER *as, SCOPE *next) {
    ARRAY_ADD(&as->scope_stack, as->active_scope);
    scope_to_scope(as, next);
}

int scope_pop(ASSEMBLER *as) {
    if(as->scope_stack.items == 0) {
        return 0;
    }
    SCOPE *prev = *ARRAY_GET(&as->scope_stack, SCOPE*, as->scope_stack.items - 1);
    as->scope_stack.items--;
    scope_to_scope(as, prev);
    return 1;
}

SCOPE *scope_find_child(SCOPE *parent, const char *name, int name_length) {
    for(int si = 0; si < parent->child_scopes.items; si++) {
        SCOPE *s = *ARRAY_GET(&parent->child_scopes, SCOPE*, si);
        if(name_length == s->scope_name_length && 0 == strnicmp(name, s->scope_name, name_length)) {
            return s;
        }
    }
    return NULL;
}

int scope_init(SCOPE *s, int type) {
    memset(s, 0, sizeof(SCOPE));
    ARRAY_INIT(&s->child_scopes, SCOPE*);
    s->symbol_table = (DYNARRAY*)malloc(sizeof(DYNARRAY) * HASH_BUCKETS);
    if(!s->symbol_table) {
        return A2_ERR;
    }

    for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        ARRAY_INIT(&s->symbol_table[bucket], SYMBOL_LABEL);
    }

    s->scope_type = type;

    return A2_OK;
}

// Recursively destroys a scope and it's children
void scope_destroy(SCOPE *s) {
    while(s->child_scopes.items) {
        scope_destroy(*ARRAY_GET(&s->child_scopes, SCOPE*, s->child_scopes.items - 1));
        s->child_scopes.items--;
    }
    array_free(&s->child_scopes);
    free(s->scope_name);
    if(s->symbol_table) {
        for(int bucket = 0; bucket < HASH_BUCKETS; bucket++) {
            array_free(&s->symbol_table[bucket]);
        }
    }
    free(s->symbol_table);
    free(s);
}

// Add a scope to the passed in parent scope, returning a pointer to the added scope
SCOPE *scope_add(ASSEMBLER *as, const char *name, const int name_length, SCOPE *parent, int type) {
    SCOPE *s = (SCOPE*)malloc(sizeof(SCOPE));
    if(!s || A2_OK != scope_init(s, type)) {
        return NULL;
    }
    if(!set_name(&s->scope_name, name, name_length)) {
        scope_destroy(s);
        return NULL;
    }
    s->scope_name_length = name_length;
    s->parent_scope = parent;
    s->scope_type = type;
    if(A2_OK != ARRAY_ADD(&parent->child_scopes, s)) {
        scope_destroy(s);
        return NULL;
    }
    return *ARRAY_GET(&parent->child_scopes, SCOPE*, parent->child_scopes.items - 1);
}

void scope_reset_ids(SCOPE *s) {
    uint32_t child_id = 0;
    s->anon_scope_id = 0;
    while(child_id < s->child_scopes.items) {
        scope_reset_ids(*ARRAY_GET(&s->child_scopes, SCOPE*, child_id++));
    }
}

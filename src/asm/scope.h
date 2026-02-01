// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct SCOPE SCOPE;

typedef struct SCOPE {
    int scope_name_length;
    int scope_type;
    int anon_scope_id;
    DYNARRAY child_scopes;
    char *scope_name;
    SCOPE *parent_scope;
    DYNARRAY *symbol_table;                                 // This is a malloc(256*SYMBOL_LABEL)
} SCOPE;

// This should eventually live with an actual string pool
char *set_name(char **s, const char *name, const int name_length);


SCOPE *scope_add(ASSEMBLER *as, const char *name, const int name_length, SCOPE *parent, int type);
SCOPE *scope_find_child(SCOPE *parent, const char *name, int name_length);
int scope_init(SCOPE *s, int type);
void scope_push(ASSEMBLER *as, SCOPE *next);
int scope_pop(ASSEMBLER *as);
void scope_reset_ids(SCOPE *s);
void scope_destroy(SCOPE *s);
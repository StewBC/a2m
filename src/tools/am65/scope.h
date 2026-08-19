// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#ifndef ASM_ASSEMBLER_TYPEDEF
#define ASM_ASSEMBLER_TYPEDEF
typedef struct ASSEMBLER ASSEMBLER;
#endif

#ifndef ASM_TARGET_TYPEDEF
#define ASM_TARGET_TYPEDEF
typedef struct TARGET TARGET;
#endif

typedef struct SCOPE {
    int has_output_redirect;
    int scope_name_length;
    int scope_type;
    int anon_scope_id;
    AM65_DYNARRAY child_scopes;
    char *scope_name;
    struct SCOPE *parent_scope;
    AM65_DYNARRAY *symbol_table;
    TARGET *output_target;   // redirect target for a named .scope file=/dest= (persists across passes)
} SCOPE;

#define ASM_SCOPE_TYPEDEF

char *set_name(char **s, const char *name, const int name_length);

SCOPE *scope_add(ASSEMBLER *as, const char *name, const int name_length, SCOPE *parent, int type);
SCOPE *scope_find_child(SCOPE *parent, const char *name, int name_length);
int scope_init(SCOPE *s, int type);
void scope_push(ASSEMBLER *as, SCOPE *next);
int scope_pop(ASSEMBLER *as);
void scope_reset_ids(SCOPE *s);
void scope_destroy(SCOPE *s);

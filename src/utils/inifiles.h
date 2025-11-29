// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    char *key;
    char *val;
} INI_KV;

typedef struct {
    char *name;
    DYNARRAY kv;      // dynamic array of pairs
} INI_SECTION;

typedef struct {
    DYNARRAY sections; // dynamic array of sections
} INI_STORE;

int ini_add(void *user_data, const char *section, const char *key, const char *value);
INI_KV *ini_find_kv(INI_SECTION *s, const char *key);
INI_SECTION *ini_find_section(INI_STORE *st, const char *name);
const char *ini_get(INI_STORE *st, const char *section, const char *key);
INI_SECTION *ini_get_or_add_section(INI_STORE *st, const char *name);
void ini_init(INI_STORE *st);
void ini_kv_free(INI_KV *kv);
int ini_remove_key(INI_STORE *st, const char *section, const char *key);
int ini_remove_section(INI_STORE *st, const char *section);
void ini_section_free(INI_SECTION *s);
int ini_set(INI_STORE *st, const char *section, const char *key, const char *value);
void ini_shutdown(INI_STORE *st);

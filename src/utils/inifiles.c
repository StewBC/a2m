// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

//  Set key to value in section (allows duplicates). Returns 0 on success.
int ini_add(void *user_data, const char *section, const char *key, const char *value) {
    INI_STORE *st = user_data;
    if(!section || !key) {
        return -1;
    }
    INI_SECTION *s = ini_get_or_add_section(st, section);
    if(!s) {
        return -1;
    }

    INI_KV new_kv;
    new_kv.key = strdup(key);
    new_kv.val = strdup(value ? value : "");
    if(!new_kv.key || !new_kv.val) {
        free(new_kv.key);
        free(new_kv.val);
        return -1;
    }
    if(A2_OK != ARRAY_ADD(&s->kv, new_kv)) {
        ini_kv_free(&new_kv);
    }
    return 0;
}

INI_KV *ini_find_kv(INI_SECTION *s, const char *key) {
    for(size_t i = 0 ; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        if(stricmp(kv->key, key) == 0) {
            return kv;
        }
    }
    return NULL;
}

INI_SECTION *ini_find_section(INI_STORE *st, const char *name) {
    for(size_t i = 0; i < st->sections.items; i++) {
        INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
        if(stricmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

const char *ini_get(INI_STORE *st, const char *section, const char *key) {
    INI_SECTION *s = ini_find_section(st, section);
    if(!s) {
        return NULL;
    }
    INI_KV *kv = ini_find_kv(s, key);
    return kv ? kv->val : NULL;
}

INI_SECTION *ini_get_or_add_section(INI_STORE *st, const char *name) {
    INI_SECTION *s = ini_find_section(st, name);
    if(s) {
        return s;
    }

    INI_SECTION new_section;
    new_section.name = strdup(name ? name : "");
    ARRAY_INIT(&new_section.kv, INI_KV);
    if(A2_OK != ARRAY_ADD(&st->sections, new_section)) {
        ini_section_free(&new_section);
    }
    return ARRAY_GET(&st->sections, INI_SECTION, st->sections.items - 1);
}

void ini_init(INI_STORE *st) {
    ARRAY_INIT(&st->sections, INI_SECTION);
}

void ini_kv_free(INI_KV *kv) {
    if(!kv) {
        return;
    }
    free(kv->key);
    free(kv->val);
}

int ini_remove_key(INI_STORE *st, const char *section, const char *key) {
    INI_SECTION *s = ini_find_section(st, section);
    if(!s) {
        return 0;
    }
    for(size_t i = 0; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        if(stricmp(kv->key, key) == 0) {
            array_remove(&s->kv, kv);
            return 1;
        }
    }
    return 0;
}

int ini_remove_section(INI_STORE *st, const char *section) {
    for(size_t i = 0; i < st->sections.items; i++) {
        INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
        if(stricmp(s->name, section) == 0) {
            ini_section_free(s);
            return 1;
        }
    }
    return 0;
}

void ini_section_free(INI_SECTION *s) {
    if(!s) {
        return;
    }
    for(size_t i = 0; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        ini_kv_free(kv);
    }
    free(s->name);
    array_free(&s->kv);
}

//  Set key in section (no dups). Returns 0 on success.
int ini_set(INI_STORE *st, const char *section, const char *key, const char *value) {
    if(!section || !key) {
        return -1;
    }
    INI_SECTION *s = ini_get_or_add_section(st, section);
    if(!s) {
        return -1;
    }
    INI_KV *kv = ini_find_kv(s, key);
    if(kv) {
        char *nv = strdup(value ? value : "");
        if(!nv) {
            return -1;
        }
        free(kv->val);
        kv->val = nv;
        return 0;
    }

    INI_KV new_kv;
    new_kv.key = strdup(key);
    new_kv.val = strdup(value ? value : "");
    if(!new_kv.key || !new_kv.val) {
        free(new_kv.key);
        free(new_kv.val);
        return -1;
    }
    if(A2_OK != ARRAY_ADD(&s->kv, new_kv)) {
        ini_kv_free(&new_kv);
    }
    return 0;
}

void ini_shutdown(INI_STORE *st) {
    if(!st) {
        return;
    }
    for(size_t i = 0; i < st->sections.items; i++) {
        INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
        ini_section_free(s);
    }
}

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

//  Set key to value in section (allows duplicates). Returns A2_OK on success.
int ini_add(void *user_data, const char *section, const char *key, const char *value) {
    INI_STORE *st = user_data;
    if(!section || !key) {
        return A2_ERR;
    }
    INI_SECTION *s = ini_get_or_add_section(st, section);
    if(!s) {
        return A2_ERR;
    }

    INI_KV new_kv;
    new_kv.key = strdup(key);
    new_kv.val = strdup(value ? value : "");
    if(!new_kv.key || !new_kv.val) {
        free(new_kv.key);
        free(new_kv.val);
        return A2_ERR;
    }
    if(A2_OK != ARRAY_ADD(&s->kv, new_kv)) {
        ini_kv_free(&new_kv);
    }
    return A2_OK;
}

// return KV or NULL
INI_KV *ini_find_kv(INI_SECTION *s, const char *key) {
    for(size_t i = 0 ; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        if(stricmp(kv->key, key) == 0) {
            return kv;
        }
    }
    return NULL;
}

// return SECTION or NULL
INI_SECTION *ini_find_section(INI_STORE *st, const char *name) {
    for(size_t i = 0; i < st->sections.items; i++) {
        INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
        if(stricmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

// return VAL or NULL
const char *ini_get(INI_STORE *st, const char *section, const char *key) {
    INI_SECTION *s = ini_find_section(st, section);
    if(!s) {
        return NULL;
    }
    INI_KV *kv = ini_find_kv(s, key);
    return kv ? kv->val : NULL;
}

// return SECTION or NULL
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
        return NULL;
    }
    return ARRAY_GET(&st->sections, INI_SECTION, st->sections.items - 1);
}

void ini_init(INI_STORE *st) {
    ARRAY_INIT(&st->sections, INI_SECTION);
}

void ini_kv_free(INI_KV *kv) {
    if(kv) {
        free(kv->key);
        free(kv->val);
    }
}

// return A2_ERR if nothing removed or A2_OK if key found and removed
int ini_remove_key(INI_STORE *st, const char *section, const char *key) {
    INI_SECTION *s = ini_find_section(st, section);
    if(s) {
        for(size_t i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            if(stricmp(kv->key, key) == 0) {
                array_remove(&s->kv, kv);
                return A2_OK;
            }
        }
    }
    return A2_ERR;
}

// return A2_ERR if nothing removed or A2_OK if section found and removed
int ini_remove_section(INI_STORE *st, const char *section) {
    for(size_t i = 0; i < st->sections.items; i++) {
        INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
        if(stricmp(s->name, section) == 0) {
            ini_section_free(s);
            return A2_OK;
        }
    }
    return A2_ERR;
}

void ini_section_free(INI_SECTION *s) {
    if(s) {
        for(size_t i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            ini_kv_free(kv);
        }
        free(s->name);
        array_free(&s->kv);
    }
}

//  Set key in section (no dups). Returns A2_OK on success, A2_ERR on error.
int ini_set(INI_STORE *st, const char *section, const char *key, const char *value) {
    if(section && key) {
        INI_SECTION *s = ini_get_or_add_section(st, section);
        if(s) {
            INI_KV *kv = ini_find_kv(s, key);
            if(kv) {
                char *nv = strdup(value ? value : "");
                if(nv) {
                    free(kv->val);
                    kv->val = nv;
                    return A2_OK;
                }
                return A2_ERR;
            }
        
            INI_KV new_kv;
            new_kv.key = strdup(key);
            new_kv.val = strdup(value ? value : "");
            if(!new_kv.key || !new_kv.val || A2_OK != ARRAY_ADD(&s->kv, new_kv)) {
                ini_kv_free(&new_kv);
                return A2_ERR;
            }
            return A2_OK;
        }
    }
    return A2_ERR;
}

void ini_shutdown(INI_STORE *st) {
    if(st) {
        for(size_t i = A2_OK; i < st->sections.items; i++) {
            INI_SECTION *s = ARRAY_GET(&st->sections, INI_SECTION, i);
            ini_section_free(s);
        }
        array_free(&st->sections);
    }
}

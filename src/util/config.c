#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ini.h"
#include "stb_ds.h"

typedef struct config_entry {
    char *section;
    char *key;
    char *value;
} config_entry;

struct config {
    config_entry *entries;
};

static char *config_strdup(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        value = "";
    }

    length = strlen(value);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static int config_find(config *cfg, const char *section, const char *key)
{
    int count;
    int i;

    if (cfg == NULL || section == NULL || key == NULL) {
        return -1;
    }

    count = arrlen(cfg->entries);
    for (i = 0; i < count; ++i) {
        config_entry *entry = &cfg->entries[i];
        if (strcmp(entry->section, section) == 0 && strcmp(entry->key, key) == 0) {
            return i;
        }
    }

    return -1;
}

static int config_parse(void *user, const char *section, const char *name, const char *value)
{
    config *cfg = (config *)user;

    config_set(cfg, section, name, value);
    return 1;
}

config *config_load(const char *path)
{
    config *cfg;

    cfg = (config *)calloc(1, sizeof(*cfg));
    if (cfg == NULL) {
        return NULL;
    }

    if (path != NULL && ini_parse(path, config_parse, cfg) < 0) {
        config_destroy(cfg);
        return NULL;
    }

    return cfg;
}

bool config_save(config *cfg, const char *path)
{
    FILE *file;
    const char *current_section;
    int count;
    int i;

    if (cfg == NULL || path == NULL) {
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    current_section = NULL;
    count = arrlen(cfg->entries);
    for (i = 0; i < count; ++i) {
        config_entry *entry = &cfg->entries[i];

        if (current_section == NULL || strcmp(current_section, entry->section) != 0) {
            if (current_section != NULL) {
                fputc('\n', file);
            }
            fprintf(file, "[%s]\n", entry->section);
            current_section = entry->section;
        }

        fprintf(file, "%s=%s\n", entry->key, entry->value);
    }

    return fclose(file) == 0;
}

const char *config_get(config *cfg, const char *section, const char *key)
{
    int index;

    index = config_find(cfg, section, key);
    if (index < 0) {
        return NULL;
    }

    return cfg->entries[index].value;
}

int config_entry_count(config *cfg)
{
    if (cfg == NULL) {
        return 0;
    }

    return arrlen(cfg->entries);
}

bool config_entry_at(
    config *cfg,
    int index,
    const char **out_section,
    const char **out_key,
    const char **out_value)
{
    config_entry *entry;

    if (cfg == NULL || index < 0 || index >= arrlen(cfg->entries)) {
        return false;
    }

    entry = &cfg->entries[index];
    if (out_section != NULL) {
        *out_section = entry->section;
    }
    if (out_key != NULL) {
        *out_key = entry->key;
    }
    if (out_value != NULL) {
        *out_value = entry->value;
    }
    return true;
}

int config_get_int(config *cfg, const char *section, const char *key, int default_value)
{
    const char *value;
    char *end;
    long parsed;

    value = config_get(cfg, section, key);
    if (value == NULL) {
        return default_value;
    }

    parsed = strtol(value, &end, 0);
    if (end == value || *end != '\0') {
        return default_value;
    }

    return (int)parsed;
}

bool config_get_bool(config *cfg, const char *section, const char *key, bool default_value)
{
    const char *value;
    char lower[8];
    size_t i;

    value = config_get(cfg, section, key);
    if (value == NULL) {
        return default_value;
    }

    for (i = 0; i < sizeof(lower) - 1 && value[i] != '\0'; ++i) {
        lower[i] = (char)tolower((unsigned char)value[i]);
    }
    lower[i] = '\0';

    if (strcmp(lower, "1") == 0 || strcmp(lower, "yes") == 0 ||
        strcmp(lower, "true") == 0 || strcmp(lower, "on") == 0) {
        return true;
    }

    if (strcmp(lower, "0") == 0 || strcmp(lower, "no") == 0 ||
        strcmp(lower, "false") == 0 || strcmp(lower, "off") == 0) {
        return false;
    }

    return default_value;
}

void config_set(config *cfg, const char *section, const char *key, const char *value)
{
    int index;
    char *new_value;
    config_entry entry;

    if (cfg == NULL || section == NULL || key == NULL) {
        return;
    }

    new_value = config_strdup(value);
    if (new_value == NULL) {
        return;
    }

    index = config_find(cfg, section, key);
    if (index >= 0) {
        free(cfg->entries[index].value);
        cfg->entries[index].value = new_value;
        return;
    }

    entry.section = config_strdup(section);
    entry.key = config_strdup(key);
    entry.value = new_value;
    if (entry.section == NULL || entry.key == NULL) {
        free(entry.section);
        free(entry.key);
        free(entry.value);
        return;
    }

    arrput(cfg->entries, entry);
}

void config_set_int(config *cfg, const char *section, const char *key, int value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%d", value);
    config_set(cfg, section, key, buffer);
}

void config_set_bool(config *cfg, const char *section, const char *key, bool value)
{
    config_set(cfg, section, key, value ? "true" : "false");
}

void config_remove_prefix(config *cfg, const char *section, const char *key_prefix)
{
    int i;
    size_t prefix_length;

    if (cfg == NULL || section == NULL || key_prefix == NULL) {
        return;
    }

    prefix_length = strlen(key_prefix);
    for (i = 0; i < arrlen(cfg->entries);) {
        config_entry *entry = &cfg->entries[i];
        if (strcmp(entry->section, section) == 0 &&
            strncmp(entry->key, key_prefix, prefix_length) == 0) {
            free(entry->section);
            free(entry->key);
            free(entry->value);
            arrdel(cfg->entries, i);
            continue;
        }
        i++;
    }
}

void config_destroy(config *cfg)
{
    int count;
    int i;

    if (cfg == NULL) {
        return;
    }

    count = arrlen(cfg->entries);
    for (i = 0; i < count; ++i) {
        free(cfg->entries[i].section);
        free(cfg->entries[i].key);
        free(cfg->entries[i].value);
    }

    arrfree(cfg->entries);
    free(cfg);
}

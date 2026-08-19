#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_ds.h"

#define SYMBOL_INDEX_INVALID UINT32_MAX
#define SYMBOL_ADDRESS_COUNT 65536u

typedef struct symbol_source_record {
    symbol_source_kind kind;
    char *name;
} symbol_source_record;

typedef struct symbol_record {
    char *name;
    uint16_t scope_path_length;
    uint16_t display_name_offset;
    uint16_t address;
    uint32_t source_id;
} symbol_record;

typedef struct symbol_name_binding {
    char *key;
    uint32_t value;
} symbol_name_binding;

struct symbol_table {
    symbol_record *entries;
    symbol_source_record *sources;
    symbol_name_binding *name_map;
    uint32_t primary_by_address[SYMBOL_ADDRESS_COUNT];
};

static void symbol_indexes_clear(symbol_table *table)
{
    size_t i;

    for (i = 0; i < SYMBOL_ADDRESS_COUNT; ++i) {
        table->primary_by_address[i] = SYMBOL_INDEX_INVALID;
    }
    shfree(table->name_map);
    table->name_map = NULL;
}

static bool symbol_source_kind_valid(symbol_source_kind kind)
{
    return kind >= SYMBOL_SOURCE_FILE && kind <= SYMBOL_SOURCE_BUILTIN;
}

static char *symbol_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1);
    return copy;
}

static void symbol_split_name(
    const char *name,
    uint16_t *out_scope_path_length,
    uint16_t *out_display_name_offset)
{
    size_t length;
    size_t last_sep;

    length = strlen(name);
    last_sep = SIZE_MAX;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (name[i] == ':' && name[i + 1] == ':') {
            last_sep = i;
            ++i;
        }
    }

    if (last_sep == SIZE_MAX) {
        *out_scope_path_length = 0;
        *out_display_name_offset = 0;
        return;
    }

    *out_scope_path_length = (uint16_t)last_sep;
    *out_display_name_offset = (uint16_t)(last_sep + 2);
}

static int symbol_find_source_id(
    const symbol_table *table,
    symbol_source_kind kind,
    const char *source_name)
{
    int count;
    int i;

    if (table == NULL || source_name == NULL) {
        return -1;
    }

    count = arrlen(table->sources);
    for (i = 0; i < count; ++i) {
        const symbol_source_record *source = &table->sources[i];
        if (source->kind == kind && strcmp(source->name, source_name) == 0) {
            return i;
        }
    }

    return -1;
}

static symbol_result symbol_get_or_add_source_id(
    symbol_table *table,
    symbol_source_kind kind,
    const char *source_name,
    uint32_t *out_source_id)
{
    int source_id;
    symbol_source_record source;

    source_id = symbol_find_source_id(table, kind, source_name);
    if (source_id >= 0) {
        *out_source_id = (uint32_t)source_id;
        return SYMBOL_OK;
    }

    source.kind = kind;
    source.name = symbol_strdup(source_name);
    if (source.name == NULL) {
        return SYMBOL_OUT_OF_MEMORY;
    }

    arrput(table->sources, source);
    *out_source_id = (uint32_t)(arrlen(table->sources) - 1);
    return SYMBOL_OK;
}

static void symbol_info_fill(
    const symbol_table *table,
    const symbol_record *entry,
    symbol_info *out_symbol)
{
    const symbol_source_record *source;

    source = &table->sources[entry->source_id];
    out_symbol->name = entry->name;
    out_symbol->scope_path = entry->scope_path_length > 0 ? entry->name : "";
    out_symbol->display_name = entry->name + entry->display_name_offset;
    out_symbol->scope_path_length = entry->scope_path_length;
    out_symbol->display_name_length = strlen(out_symbol->display_name);
    out_symbol->address = entry->address;
    out_symbol->source_kind = source->kind;
    out_symbol->source_name = source->name;
}

static void symbol_free_entry(symbol_record *entry)
{
    free(entry->name);
    entry->name = NULL;
}

static void symbol_rebuild_indexes(symbol_table *table)
{
    int count;
    int i;

    symbol_indexes_clear(table);

    count = arrlen(table->entries);
    for (i = 0; i < count; ++i) {
        symbol_record *entry = &table->entries[i];
        table->primary_by_address[entry->address] = (uint32_t)i;
        shput(table->name_map, entry->name, (uint32_t)i);
    }
}

static bool symbol_entry_matches_identity(
    const symbol_record *entry,
    uint16_t address,
    const char *name,
    uint32_t source_id)
{
    return entry->address == address &&
        entry->source_id == source_id &&
        strcmp(entry->name, name) == 0;
}

static void symbol_remove_address_conflicts(symbol_table *table, uint16_t address)
{
    int i;

    for (i = 0; i < arrlen(table->entries);) {
        symbol_record *entry = &table->entries[i];
        if (entry->address == address) {
            symbol_free_entry(entry);
            arrdel(table->entries, i);
            continue;
        }
        ++i;
    }

    symbol_rebuild_indexes(table);
}

symbol_table *symbol_table_create(void)
{
    symbol_table *table;

    table = (symbol_table *)calloc(1, sizeof(*table));
    if (table == NULL) {
        return NULL;
    }

    symbol_indexes_clear(table);
    return table;
}

void symbol_table_destroy(symbol_table *table)
{
    if (table == NULL) {
        return;
    }

    symbol_table_clear(table);
    free(table);
}

void symbol_table_clear(symbol_table *table)
{
    int i;

    if (table == NULL) {
        return;
    }

    for (i = 0; i < arrlen(table->entries); ++i) {
        symbol_free_entry(&table->entries[i]);
    }
    arrfree(table->entries);

    for (i = 0; i < arrlen(table->sources); ++i) {
        free(table->sources[i].name);
    }
    arrfree(table->sources);

    symbol_indexes_clear(table);
}

symbol_result symbol_table_add(
    symbol_table *table,
    uint16_t address,
    const char *name,
    symbol_source_kind source_kind,
    const char *source_name,
    bool overwrite)
{
    int existing_source_id;
    uint32_t address_index;
    uint32_t source_id;
    symbol_record entry;
    symbol_result source_result;

    if (table == NULL || name == NULL || name[0] == '\0' ||
        source_name == NULL || !symbol_source_kind_valid(source_kind)) {
        return SYMBOL_INVALID;
    }

    existing_source_id = symbol_find_source_id(table, source_kind, source_name);
    if (existing_source_id >= 0) {
        int i;
        for (i = 0; i < arrlen(table->entries); ++i) {
            if (symbol_entry_matches_identity(
                    &table->entries[i],
                    address,
                    name,
                    (uint32_t)existing_source_id)) {
                return SYMBOL_ALREADY_EXISTS;
            }
        }
    }

    address_index = table->primary_by_address[address];

    if (!overwrite && address_index != SYMBOL_INDEX_INVALID) {
        return SYMBOL_CONFLICT;
    }

    source_result = symbol_get_or_add_source_id(table, source_kind, source_name, &source_id);
    if (source_result != SYMBOL_OK) {
        return source_result;
    }

    entry.name = symbol_strdup(name);
    if (entry.name == NULL) {
        return SYMBOL_OUT_OF_MEMORY;
    }
    symbol_split_name(entry.name, &entry.scope_path_length, &entry.display_name_offset);
    entry.address = address;
    entry.source_id = source_id;

    if (overwrite) {
        symbol_remove_address_conflicts(table, address);
    }

    arrput(table->entries, entry);
    symbol_rebuild_indexes(table);

    return overwrite && address_index != SYMBOL_INDEX_INVALID ?
        SYMBOL_REPLACED :
        SYMBOL_OK;
}

symbol_result symbol_table_remove_source(
    symbol_table *table,
    symbol_source_kind source_kind,
    const char *source_name)
{
    int source_id;
    int removed;
    int i;

    if (table == NULL || source_name == NULL || !symbol_source_kind_valid(source_kind)) {
        return SYMBOL_INVALID;
    }

    source_id = symbol_find_source_id(table, source_kind, source_name);
    if (source_id < 0) {
        return SYMBOL_NOT_FOUND;
    }

    removed = 0;
    for (i = 0; i < arrlen(table->entries);) {
        symbol_record *entry = &table->entries[i];
        if (entry->source_id == (uint32_t)source_id) {
            symbol_free_entry(entry);
            arrdel(table->entries, i);
            removed = 1;
            continue;
        }
        ++i;
    }

    if (!removed) {
        return SYMBOL_NOT_FOUND;
    }

    symbol_rebuild_indexes(table);
    return SYMBOL_OK;
}

symbol_result symbol_table_remove_kind(
    symbol_table *table,
    symbol_source_kind source_kind)
{
    int removed = 0;
    int i;

    if (table == NULL || !symbol_source_kind_valid(source_kind)) {
        return SYMBOL_INVALID;
    }

    for (i = 0; i < arrlen(table->entries);) {
        symbol_record *entry = &table->entries[i];
        if (table->sources[entry->source_id].kind == source_kind) {
            symbol_free_entry(entry);
            arrdel(table->entries, i);
            removed = 1;
            continue;
        }
        ++i;
    }

    if (!removed) {
        return SYMBOL_NOT_FOUND;
    }

    symbol_rebuild_indexes(table);
    return SYMBOL_OK;
}

symbol_result symbol_table_find_by_address(
    const symbol_table *table,
    uint16_t address,
    symbol_info *out_symbol)
{
    uint32_t index;

    if (table == NULL || out_symbol == NULL) {
        return SYMBOL_INVALID;
    }

    index = table->primary_by_address[address];
    if (index == SYMBOL_INDEX_INVALID) {
        return SYMBOL_NOT_FOUND;
    }

    symbol_info_fill(table, &table->entries[index], out_symbol);
    return SYMBOL_OK;
}

symbol_result symbol_table_find_by_name(
    const symbol_table *table,
    const char *name,
    symbol_info *out_symbol)
{
    int map_index;
    uint32_t entry_index;
    symbol_name_binding *name_map;

    if (table == NULL || name == NULL || out_symbol == NULL) {
        return SYMBOL_INVALID;
    }

    name_map = table->name_map;
    map_index = shgeti(name_map, name);
    if (map_index < 0) {
        return SYMBOL_NOT_FOUND;
    }

    entry_index = name_map[map_index].value;
    symbol_info_fill(table, &table->entries[entry_index], out_symbol);
    return SYMBOL_OK;
}

symbol_result symbol_table_find_nearest_before(
    const symbol_table *table,
    uint16_t address,
    uint16_t max_offset,
    symbol_info *out_symbol,
    uint16_t *out_offset)
{
    uint32_t offset;

    if (table == NULL || out_symbol == NULL || out_offset == NULL) {
        return SYMBOL_INVALID;
    }

    for (offset = 0; offset <= max_offset && offset <= address; ++offset) {
        uint16_t candidate = (uint16_t)(address - offset);
        uint32_t index = table->primary_by_address[candidate];
        if (index != SYMBOL_INDEX_INVALID) {
            symbol_info_fill(table, &table->entries[index], out_symbol);
            *out_offset = (uint16_t)offset;
            return SYMBOL_OK;
        }
    }

    return SYMBOL_NOT_FOUND;
}

size_t symbol_table_count(const symbol_table *table)
{
    return table == NULL ? 0u : (size_t)arrlen(table->entries);
}

symbol_result symbol_table_get(
    const symbol_table *table,
    size_t index,
    symbol_info *out_symbol)
{
    if (table == NULL || out_symbol == NULL) {
        return SYMBOL_INVALID;
    }
    if (index >= (size_t)arrlen(table->entries)) {
        return SYMBOL_NOT_FOUND;
    }

    symbol_info_fill(table, &table->entries[index], out_symbol);
    return SYMBOL_OK;
}

static symbol_lookup_result symbol_resolver_address_to_label(
    void *userdata,
    uint16_t address,
    char *out_label,
    size_t out_label_size)
{
    symbol_info info;

    if (out_label != NULL && out_label_size > 0) {
        out_label[0] = '\0';
    }
    if (out_label == NULL || out_label_size == 0) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    if (symbol_table_find_by_address((const symbol_table *)userdata, address, &info) != SYMBOL_OK) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    snprintf(out_label, out_label_size, "%s", info.display_name);
    return SYMBOL_LOOKUP_FOUND;
}

static symbol_lookup_result symbol_resolver_label_to_address(
    void *userdata,
    const char *label,
    uint16_t *out_address)
{
    symbol_info info;

    if (out_address == NULL) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    if (symbol_table_find_by_name((const symbol_table *)userdata, label, &info) != SYMBOL_OK) {
        return SYMBOL_LOOKUP_NOT_FOUND;
    }

    *out_address = info.address;
    return SYMBOL_LOOKUP_FOUND;
}

static size_t symbol_resolver_enumerate(
    void *userdata,
    symbol_entry *out_entries,
    size_t max_entries)
{
    symbol_table *table = (symbol_table *)userdata;
    size_t count;
    size_t copy_count;
    size_t i;

    count = symbol_table_count(table);
    copy_count = count < max_entries ? count : max_entries;

    if (out_entries != NULL) {
        for (i = 0; i < copy_count; ++i) {
            out_entries[i].label = table->entries[i].name;
            out_entries[i].scope_path = table->entries[i].scope_path_length > 0 ?
                table->entries[i].name :
                "";
            out_entries[i].display_name =
                table->entries[i].name + table->entries[i].display_name_offset;
            out_entries[i].scope_path_length = table->entries[i].scope_path_length;
            out_entries[i].display_name_length = strlen(out_entries[i].display_name);
            out_entries[i].address = table->entries[i].address;
        }
    }

    return count;
}

void symbol_table_make_resolver(symbol_table *table, symbol_resolver *resolver)
{
    if (resolver == NULL) {
        return;
    }

    if (table == NULL) {
        symbol_resolver_null(resolver);
        return;
    }

    resolver->userdata = table;
    resolver->address_to_label = symbol_resolver_address_to_label;
    resolver->label_to_address = symbol_resolver_label_to_address;
    resolver->enumerate = symbol_resolver_enumerate;
}

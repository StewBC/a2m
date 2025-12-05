// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "rt_lib.h"

int rt_sym_add_symbol(RUNTIME *rt, const char *symbol_source, const char *symbol_name, size_t symbol_name_length, uint16_t address, int overwrite) {
    // Find where to insert the symbol
    enum {ACTION_NONE, ACTION_ADD, ACTION_UPDATE};
    DYNARRAY *bucket = &rt->symbols[address & 0xff];
    int symbol_index, action = ACTION_ADD, items = bucket->items;
    for(symbol_index = 0; symbol_index < items; symbol_index++) {
        // Sort the address, low to high, into the array ("bucket)"
        SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
        if(address <= symbol->pc) {
            // If the address already has a name, maybe skip it
            action = 2;
            if(address == symbol->pc) {
                if(overwrite) {
                    free(symbol->symbol_name);
                } else {
                    action = ACTION_NONE;
                }
            } else {
                array_copy_items(bucket, symbol_index, items, symbol_index + 1);
            }
            break;
        }
    }
    // Insert the symbol into the array if the address isn't already named
    if(action) {
        if(ACTION_ADD == action) {
            // Append this symbol to the end
            if(A2_OK != array_add(bucket, NULL)) {
                return A2_ERR;
            }
        }
        SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
        symbol->pc = address;
        symbol->symbol_source = symbol_source;
        symbol->symbol_name = util_strndup(symbol_name, symbol_name_length);
        if(!symbol->symbol_name) {
            return A2_ERR;
        }
    }
    return A2_OK;
}

int rt_sym_add_symbols(RUNTIME *rt, const char *symbol_source, char *input, size_t data_length, int overwrite) {
    int state = 0;
    char *end = input + data_length;
    while(input < end) {
        unsigned int value;
        int parsed;
        // Search for a hex string at the start of a line
        if(1 == sscanf(input, "%x%n", &value, &parsed)) {
            uint16_t address = value;
            input += parsed;
            char *symbol_name = input + strspn(input, " \t");
            char *symbol_end = strpbrk(symbol_name, " \t\n\r");
            if(!symbol_end) {
                symbol_end = end;
            }
            int symbol_length = symbol_end - symbol_name;
            input = symbol_end;
            if(A2_OK != rt_sym_add_symbol(rt, symbol_source, symbol_name, symbol_length, address, overwrite)) {
                return A2_ERR;
            }
        }
        // Find the end of the line
        while(input < end && *input && !util_is_newline(*input)) {
            input++;
        }
        // Skip past the end of the line
        while(input < end && *input && util_is_newline(*input)) {
            input++;
        }
    }
    return A2_OK;
}

int rt_sym_init(RUNTIME *rt, INI_STORE *ini_store) {
    // Make the 256 symbols "buckets"
    rt->symbols = (DYNARRAY *) malloc(sizeof(DYNARRAY) * 256);
    if(!rt->symbols) {
        A2_ERR;
    }

    for(int i = 0; i < 256; i++) {
        DYNARRAY *s = &rt->symbols[i];
        ARRAY_INIT(s, SYMBOL);
    }

    // Init the search array
    ARRAY_INIT(&rt->symbols_search, SYMBOL *);

    // Load the symbols (no error if not loaded)
    UTIL_FILE symbol_file;
    memset(&symbol_file, 0, sizeof(symbol_file));

    // Load the symbol files if they are found
    if(A2_OK == util_file_load(&symbol_file, "symbols/A2_BASIC.SYM", "r")) {
        rt_sym_add_symbols(rt, "A2_BASIC", symbol_file.file_data, symbol_file.file_size, 0);
    }
    if(A2_OK == util_file_load(&symbol_file, "symbols/APPLE2E.SYM", "r")) {
        rt_sym_add_symbols(rt, "APPLE2E", symbol_file.file_data, symbol_file.file_size, 0);
    }
    if(A2_OK == util_file_load(&symbol_file, "symbols/USER.SYM", "r")) {
        rt_sym_add_symbols(rt, "USER", symbol_file.file_data, symbol_file.file_size, 1);
    }
    if(A2_OK != rt_sym_search_update(rt)) {
        return A2_ERR;
    }

    return A2_OK;
}

void rt_sym_shutdown(RUNTIME *rt) {
    for(int i = 0; i < 256; i++) {
        for(int si = 0; si < rt->symbols[i].items; si++) {
            SYMBOL *s = ARRAY_GET(&rt->symbols[i], SYMBOL, si);
            free(s->symbol_name);
        }
        array_free(&rt->symbols[i]);
    }
    free(rt->symbols);
    array_free(&rt->symbols_search);
}

int symbols_sort(const void *lhs, const void *rhs) {
    SYMBOL *symb_lhs = *(SYMBOL **)lhs;
    SYMBOL *symb_rhs = *(SYMBOL **)rhs;
    int result = stricmp(symb_lhs->symbol_source, symb_rhs->symbol_source);
    if(!result) {
        return stricmp(symb_lhs->symbol_name, symb_rhs->symbol_name);
    }
    return result;
}

void rt_sym_remove_symbols(RUNTIME *rt, const char *symbol_source) {
    size_t bucket_index;
    for(bucket_index = 0; bucket_index < 256; bucket_index++) {
        size_t symbol_index = 0;
        DYNARRAY *bucket = &rt->symbols[bucket_index];
        while(symbol_index < bucket->items) {
            SYMBOL *symbol = ARRAY_GET(bucket, SYMBOL, symbol_index);
            if(0 == strcmp(symbol_source, symbol->symbol_source)) {
                free(symbol->symbol_name);
                array_remove(bucket, symbol);
                continue;
            }
            symbol_index++;
        }
    }
}

int rt_sym_search_update(RUNTIME *rt) {
    size_t index;
    int count = 0;
    DYNARRAY *search = &rt->symbols_search;

    // Clear the search (also good if there's an error)
    search->items = 0;
    // See how many symbols there are
    for(index = 0; index < 256; index++) {
        count += rt->symbols[index].items;
    }
    // Make room for a list that size
    if(A2_OK != array_resize(search, count)) {
        return A2_ERR;
    }
    // Populate the search list
    for(index = 0; index < 256; index++) {
        size_t bucket_index;
        DYNARRAY *bucket = &rt->symbols[index];
        for(bucket_index = 0; bucket_index < bucket->items; bucket_index++) {
            SYMBOL *s = ARRAY_GET(bucket, SYMBOL, bucket_index);
            if(A2_OK != ARRAY_ADD(search, s)) {
                array_free(search);
                return A2_ERR;
            }
        }
    }
    // Sort by source, name
    qsort(search->data, search->items, search->element_size, symbols_sort);
    return A2_OK;
}

char *rt_sym_find_symbols(RUNTIME *rt, uint32_t address) {
    DYNARRAY *s = &rt->symbols[address & 0xff];
    int lo = 0;
    int hi = s->items - 1;

    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        SYMBOL *sym = ARRAY_GET(s, SYMBOL, mid);
        if (sym->pc < address) {
            lo = mid + 1;
        } else if (sym->pc > address) {
            hi = mid - 1;
        } else {
            return sym->symbol_name;
        }
    }

    return NULL;
}

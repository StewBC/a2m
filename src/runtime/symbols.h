// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct SYMBOL {
    uint16_t pc;
    char *symbol_name;
    const char *symbol_source;
} SYMBOL;

int symbols_add_symbol(RUNTIME *rt, const char *symbol_source, const char *symbol_name, size_t symbol_name_length, uint16_t address, int overwrite);
int symbols_add_symbols(RUNTIME *rt, const char *symbol_source, char *input, size_t data_length, int overwrite);
int symbols_init(RUNTIME *rt, INI_STORE *ini_store);
void symbols_remove_symbols(RUNTIME *rt, const char *symbol_source);
int symbols_search_update(RUNTIME *rt);
char *symbols_find_symbols(RUNTIME *rt, uint32_t address);

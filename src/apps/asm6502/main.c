// 6502 command line assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"

#include "utils_lib.h"
#include "asm_lib.h"

typedef struct RAM {
    uint8_t RAM_MAIN[64 * 1024];
} RAM;

typedef struct MACHINE {
    ASSEMBLER as;
    RAM ram;
} MACHINE;

// This is the command line assembler version - it just uses a flat 64K buffer
void output_byte_at_address(void *user, uint16_t address, uint8_t byte_value) {
    MACHINE *m = (MACHINE *)user;
    if(m->as.verbose) {
        if(!(address % 16) || address != m->as.last_address) {
            printf("\n%04X: ", address);
        }
        printf("%02X ", byte_value);
    }
    m->ram.RAM_MAIN[address] = byte_value;
}

int get_argument(int argc, char *argv[], const char *key, const char **value) {
    for(int i = 1; i < argc; ++i) {
        if(stricmp(argv[i], key) == 0 && (i + 1 < argc || !value)) {
            if(value) {
                *value = argv[i + 1];
            }
            return 1;
        }
    }
    return 0;
}

void usage(char *program_name) {
    char *c = program_name + strlen(program_name) - 1;
    while(c > program_name) {
        if(*c == '/' || *c == '\\') {
            c++;
            break;
        }
        c--;
    }

    fprintf(stderr, "Usage: %s <-i infile> [-o outfile] [-s symbolfile] [-v]\n", c);
    fprintf(stderr, "Where: infile is a 6502 assembly language file\n");
    fprintf(stderr, "       outfile will be a binary file containing the assembled 6502\n");
    fprintf(stderr, "       symbolfile contains a list of the addresses of all the named variables and labels\n");
    fprintf(stderr, "       -v turns on verbose and will dump the hex 6502 as it is assembled\n");
}

static void save_symbols(UTIL_FILE *sf, SCOPE *s, int level) {
    size_t bucket, i;
    // Accumulate all symbols
    DYNARRAY symbols;
    ARRAY_INIT(&symbols, SYMBOL_LABEL*);
    for(bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        DYNARRAY *b = &s->symbol_table[bucket];
        for(i = 0; i < b->items; i++) {
            SYMBOL_LABEL *sl = ARRAY_GET(b, SYMBOL_LABEL, i);
            ARRAY_ADD(&symbols, sl);
        }
    }

    // Sort the symbols
    qsort(symbols.data, symbols.items, sizeof(SYMBOL_LABEL*), symbol_sort);
    
    // Write the sorted symbols to a file
    fprintf(sf->fp, "%*s%s: %.*s {\n", level * 2, "", s->scope_type == GPERF_DOT_SCOPE ? "Scope" : "Proc", s->scope_name_length, s->scope_name);
    level++;
    for(i = 0; i < symbols.items; i++) {
        SYMBOL_LABEL **sl = ARRAY_GET(&symbols, SYMBOL_LABEL*, i);
        fprintf(sf->fp, "%*s%04X %.*s \n", level * 2, "", (uint16_t)(*sl)->symbol_value, (int)(*sl)->symbol_length, (*sl)->symbol_name);
    }
    level--;
    array_free(&symbols);
    for(int csi = 0; csi < s->child_scopes.items; csi++) {
        fprintf(sf->fp, "\n");
        save_symbols(sf, ARRAY_GET(&s->child_scopes, SCOPE, csi), level + 1);
    }
    fprintf(sf->fp, "%*s}\n", level * 2, "");
}

int main(int argc, char **argv) {
    const char *output_file_name = 0;
    const char *symbol_file_name = 0;
    const char *input_file;
    int required = 1;
    MACHINE m;
    ERRORLOG errorlog;

    // Set the "machine" RAM to all 0's
    memset(m.ram.RAM_MAIN, 0, 64 * 1024);
    errlog_init(&errorlog);

    if(A2_OK != assembler_init(&m.as, &errorlog, &m, output_byte_at_address)) {
        exit(1);
    }

    required &= get_argument(argc, argv, "-i", &input_file);
    get_argument(argc, argv, "-o", &output_file_name);
    m.as.verbose = get_argument(argc, argv, "-v", NULL);
    get_argument(argc, argv, "-s", &symbol_file_name);

    if(!required) {
        usage(argv[0]);
        exit(1);
    }

    if(A2_OK != assembler_assemble(&m.as, input_file, 0)) {
        fprintf(stderr, "File %s could not be opened\n", input_file);
        exit(1);
    }

    if(errorlog.log_array.items) {
        // Second pass errors get reported
        fprintf(stderr, "\nAssembly errors:\n");
        for(size_t i = 0; i < errorlog.log_array.items; i++) {
            ERROR_ENTRY *e = ARRAY_GET(&errorlog.log_array, ERROR_ENTRY, i);
            if(e->supressed) {
                fprintf(stderr, "%s [%zu supressed]\n", e->err_str, e->supressed);
            } else {
                fprintf(stderr, "%s\n", e->err_str);
            }
        }
    }

    // Write the output to a file
    if(output_file_name && m.as.start_address < m.as.last_address) {
        UTIL_FILE output_file;
        memset(&output_file, 0, sizeof(UTIL_FILE));
        if(A2_OK != util_file_open(&output_file, output_file_name, "wb")) {
            fprintf(stderr, "Could not open output file %s for writing\n", output_file_name);
        } else {
            fwrite(&m.ram.RAM_MAIN[m.as.start_address], 1, m.as.last_address - m.as.start_address, output_file.fp);
        }
        util_file_discard(&output_file);
    }

    // sort the symbol table by address and write to a file
    if(symbol_file_name) {
        UTIL_FILE symbol_file;
        memset(&symbol_file, 0, sizeof(UTIL_FILE));
        if(A2_OK != util_file_open(&symbol_file, symbol_file_name, "w")) {
            fprintf(stderr, "Could not open output file %s for writing\n", symbol_file_name);
        } else {
            save_symbols(&symbol_file, &m.as.root_scope, 0);
        }
        util_file_discard(&symbol_file);
    }

    assembler_shutdown(&m.as);
    errlog_shutdown(&errorlog);
    puts("\nDone.\n");
    return 0;
}

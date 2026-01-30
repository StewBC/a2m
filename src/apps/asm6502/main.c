// 6502 command line assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"

#include "utils_lib.h"
#include "asm_lib.h"

typedef struct {
    uint8_t RAM_MAIN[64 * 1024];
} RAM;

typedef struct {
    ASSEMBLER as;
    RAM ram;
    uint16_t emit_start;
    uint16_t emit_end;
} MACHINE;

static const char *type_str[] = {
    "UNK",
    "VAR",
    "ADR"
};

// This is the command line assembler version - it just uses a flat 64K buffer
void output_byte_at_address(void *user, uint16_t address, uint8_t byte_value) {
    MACHINE *m = (MACHINE *)user;
    if(address < m->emit_start) {
        m->emit_start = address;
    }
    if(address > m->emit_end) {
        m->emit_end = address;
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

    fprintf(stderr, "Usage: %s <-i infile> [-o outfile] [-s <symbolfile|->] [-v]\n", c);
    fprintf(stderr, "Where: infile is a 6502 assembly language file\n");
    fprintf(stderr, "       outfile will be a binary file containing the assembled 6502\n");
    fprintf(stderr, "       symbolfile contains a list all variables, labels and segment addresses\n");
    fprintf(stderr, "       symbolfile name as a '-' character sends output to stdout\n");
    fprintf(stderr, "       -v turns on verbose and will dump the hex 6502\n");
}

// Sort by value
int symbol_sort(const void *lhs, const void *rhs) {
    return (uint16_t)((*(SYMBOL_LABEL**)lhs)->symbol_value) - (uint16_t)((*(SYMBOL_LABEL**)rhs)->symbol_value);
}

static void save_symbols(FILE *fp, SCOPE *s, int level) {
    size_t bucket, i;
    // Accumulate all symbols - add ptrs to the symbols array
    DYNARRAY symbols;
    ARRAY_INIT(&symbols, SYMBOL_LABEL*);
    for(bucket = 0; bucket < HASH_BUCKETS; bucket++) {
        DYNARRAY *b = &s->symbol_table[bucket];
        for(i = 0; i < b->items; i++) {
            SYMBOL_LABEL *sl = ARRAY_GET(b, SYMBOL_LABEL, i);
            ARRAY_ADD(&symbols, sl);
        }
    }

    // Sort the symbols array (ptrs to symbols)
    qsort(symbols.data, symbols.items, sizeof(SYMBOL_LABEL*), symbol_sort);
    
    // Write the sorted symbols to a file
    fprintf(fp, "%*s%s: %.*s {\n", level * 2, "", s->scope_type == GPERF_DOT_SCOPE ? "Scope" : "Proc", s->scope_name_length, s->scope_name);
    level++;
    for(i = 0; i < symbols.items; i++) {
        SYMBOL_LABEL **sl = ARRAY_GET(&symbols, SYMBOL_LABEL*, i);
        fprintf(fp, "%*s%04X %s %.*s\n", level * 2, "", (uint16_t)(*sl)->symbol_value, type_str[(*sl)->symbol_type], (int)(*sl)->symbol_length, (*sl)->symbol_name);
    }
    level--;
    array_free(&symbols);
    for(int csi = 0; csi < s->child_scopes.items; csi++) {
        fprintf(fp, "\n");
        save_symbols(fp, *ARRAY_GET(&s->child_scopes, SCOPE*, csi), level + 1);
    }
    fprintf(fp, "%*s}\n", level * 2, "");
}

static void save_segments(FILE *fp, DYNARRAY *segments) {
    if(!segments->items) {
        return;
    }
    fprintf(fp, "\nSEGMENTS {");
    for(int si = 0; si < segments->items; si++) {
        SEGMENT *s = ARRAY_GET(segments, SEGMENT, si);
        fprintf(fp, "\n  %.*s [%04X-%04X)", s->segment_name_length, s->segment_name, s->segment_start_address, s->segment_output_address);
    }
    fprintf(fp, "\n}\n");
}

int main(int argc, char **argv) {
    const char *output_file_name = 0;
    const char *symbol_file_name = 0;
    const char *input_file;
    int required = 1;
    MACHINE m;
    ERRORLOG errorlog;

    // Set the "machine" RAM to all 0's
    memset(&m, 0, sizeof(MACHINE));
    m.emit_start = 0xFFFF;
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

    // Write output to stdout if requited
    if(m.as.verbose) {
        for(int address = m.emit_start; address <= m.emit_end; address++) {
            uint8_t byte_value = m.ram.RAM_MAIN[address];
            if(0 == (address % 16)) {
                printf("\n%04X: ", address);
            }
            printf("%02X ", byte_value);
        }
        printf("\n\n");
    }

    // Write the output to a file
    if(output_file_name && m.emit_start < m.emit_end) {
        UTIL_FILE output_file;
        memset(&output_file, 0, sizeof(UTIL_FILE));
        if(A2_OK != util_file_open(&output_file, output_file_name, "wb")) {
            fprintf(stderr, "Could not open output file %s for writing\n", output_file_name);
        } else {
            fwrite(&m.ram.RAM_MAIN[m.emit_start], 1, m.emit_end - m.emit_start, output_file.fp);
        }
        util_file_discard(&output_file);
    }

    // sort the symbol table by address and write to a file
    if(symbol_file_name) {
        UTIL_FILE symbol_file;
        FILE *fp;
        memset(&symbol_file, 0, sizeof(UTIL_FILE));
        if(0 == strcmp(symbol_file_name, "-")) {
            fp = stdout;
        } else {
            if(A2_OK != util_file_open(&symbol_file, symbol_file_name, "w")) {
                fprintf(stderr, "Could not open output file %s for writing\n", symbol_file_name);
            }
            fp = symbol_file.fp;
        }
        if(fp) {
            save_symbols(fp, m.as.root_scope, 0);
            save_segments(fp, &m.as.segments);
        }
        util_file_discard(&symbol_file);
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
    
    // If segments were used, show gaps
    if(m.as.segments.items) {
        uint32_t emit_byte = 0, issue = 0;
        uint32_t emit_end = 0;
        for(int si = 0; si < m.as.segments.items; si++) {
            SEGMENT *s = ARRAY_GET(&m.as.segments, SEGMENT, si);
            if(s->do_not_emit) {
                continue;
            }
            if(!emit_byte) {
                emit_end = s->segment_output_address;
                emit_byte = 1;
            } else {
                if(s->segment_start_address > emit_end) {
                    fprintf(stderr, "\nSegment %.*s can start at $%04X*", s->segment_name_length, s->segment_name, emit_end);
                    issue = 1;
                }
                uint16_t seg_size = s->segment_output_address - s->segment_start_address;
                emit_end += seg_size;
            }
        }
        if(issue) {
            fprintf(stderr, "\n* Offsets may slightly off, based on .align statement changing output size\n");
        }
    }
    assembler_shutdown(&m.as);
    errlog_shutdown(&errorlog);
    puts("\nDone.\n");
    return 0;
}

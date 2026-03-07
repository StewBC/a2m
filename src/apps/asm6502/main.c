// 6502 command line assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"

#include "utils_lib.h"
#include "asm_lib.h"

typedef struct {
    char *file_name;
    uint8_t RAM_MAIN[64 * 1024];
} FILE_TARGET;

typedef struct {
    ASSEMBLER as;
    uint32_t verbose;
} FILE_REDIR_CTX;

static const char *type_str[] = {
    "VAR",
    "ADR",
    "UNK",
    "LCL"
};

static void write_ram_to_file(TARGET *target) {
    FILE_TARGET *ft = target->target_ctx;
     // Write the output to a file
    if(ft->file_name) {
        UTIL_FILE output_file;
        memset(&output_file, 0, sizeof(UTIL_FILE));
        if(A2_OK != util_file_open(&output_file, ft->file_name, "wb")) {
            fprintf(stderr, "Could not open output file %s for writing\n", ft->file_name);
        } else {
            int start = -1;
            for(int si = 0; si < target->segments.items; si++) {
                SEGMENT *s = *ARRAY_GET(&target->segments, SEGMENT*, si);
                if(s->do_not_emit || s->segment_start_address == s->segment_output_address) {
                    continue;
                }
                if(start < 0) {
                    start = s->segment_start_address;
                }
                int end = s->segment_output_address;
                if(end > start) {
                    fwrite(&ft->RAM_MAIN[start], 1, end - start, output_file.fp);
                    start = end;
                }
            }
        }
        util_file_discard(&output_file);
    }
}

// This is the command line assembler version - it just uses a flat 64K buffer, per target
void output_byte_at_address(void *user, uint16_t address, uint8_t byte_value) {
    FILE_TARGET *ft = (FILE_TARGET *)user;
    ft->RAM_MAIN[address] = byte_value;
}

void *output_redirect_start_file(void *user, const char *file_name, int file_name_length, const char *bank_name, int bank_name_length) {
    UNUSED(bank_name);
    UNUSED(bank_name_length);
    FILE_TARGET *ft = NULL;
    FILE_REDIR_CTX *fr_ctx = (FILE_REDIR_CTX *)user;
    ASSEMBLER *as = &fr_ctx->as;
    if(file_name_length) {
        ft = malloc(sizeof(FILE_TARGET));
        if(ft) {
            memset(ft, 0, sizeof(FILE_TARGET));
            ft->file_name = util_strndup(file_name, file_name_length);
            if(!ft->file_name) {
                asm_err(as, ASM_ERR_FATAL, "Out of memory");
                free(ft);
                ft = NULL;
            }
        }
    } else {
        asm_err(as, ASM_ERR_RESOLVE, "Named scope redirect did not provide a file name");
    }
    return ft;
}

void output_redirect_end_file(void *asm_ctx_user, void *target_ctx) {
    UNUSED(asm_ctx_user);
    UNUSED(target_ctx);
}

void file_target_release(void *user) {
    FILE_TARGET *ft = (FILE_TARGET *)user;
    free(ft->file_name);
    free(ft);
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
    uint32_t scope_hash = util_fnv_1a_hash(s->scope_name, s->scope_name_length);
    SYMBOL_LABEL *scope_sym = symbol_lookup_chain(s, scope_hash, s->scope_name, s->scope_name_length);
    const char *scope_kind = (s->scope_type == GPERF_DOT_SCOPE) ? "SCOPE" : "PROC";

    if(scope_sym) {
        fprintf(fp, "%*s%04X %s %.*s {\n",
            level * 2, "",
            (uint16_t)scope_sym->symbol_value,
            scope_kind,
            (int)s->scope_name_length, s->scope_name);
    } else {
        fprintf(fp, "%*s%s %.*s {\n",
            level * 2, "",
            scope_kind,
            (int)s->scope_name_length, s->scope_name);
    }

    level++;

    for(i = 0; i < symbols.items; i++) {
        SYMBOL_LABEL *sl = *ARRAY_GET(&symbols, SYMBOL_LABEL*, i);
        fprintf(fp, "%*s%04X %s %.*s\n",
            level * 2, "",
            (uint16_t)sl->symbol_value,
            type_str[sl->symbol_type],
            (int)sl->symbol_length, sl->symbol_name);
    }

    level--;
    array_free(&symbols);
    for(int csi = 0; csi < s->child_scopes.items; csi++) {
        fprintf(fp, "\n");
        save_symbols(fp, *ARRAY_GET(&s->child_scopes, SCOPE*, csi), level + 1);
    }
    fprintf(fp, "%*s}\n", level * 2, "");
}

static void save_segments(FILE *fp, DYNARRAY *targets) {
    for(int i = 0; i < targets->items; i++) {
        TARGET *target = *ARRAY_GET(targets, TARGET*, i);
        fprintf(fp, "\nSEGMENTS {");
        for(int si = 0; si < target->segments.items; si++) {
            SEGMENT *s = *ARRAY_GET(&target->segments, SEGMENT*, si);
            if(s->segment_start_address != s->segment_output_address) {
                fprintf(fp, "\n  %.*s [%04X-%04X)", s->segment_name_length, s->segment_name, s->segment_start_address, s->segment_output_address);
            }
        }
        fprintf(fp, "\n}\n");
    }
}

static void show_segment_gaps(DYNARRAY *targets) {
    for(int i = 0; i < targets->items; i++) {
        TARGET *target = *ARRAY_GET(targets, TARGET*, i);
        // Any segments outside the unnamed standard segment
        if(target->segments.items > 1) {
            uint32_t emit = 0, issue = 0;
            uint32_t emit_end = 0;
            for(int si = 0; si < target->segments.items; si++) {
                SEGMENT *s = *ARRAY_GET(&target->segments, SEGMENT*, si);
                if(s->do_not_emit || s->segment_start_address == s->segment_output_address) {
                    continue;
                }
                if(!emit) {
                    emit_end = s->segment_output_address;
                    emit = 1;
                } else {
                    if(s->segment_start_address != emit_end) {
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
    }
}
int main(int argc, char **argv) {
    const char *output_file_name = 0;
    const char *symbol_file_name = 0;
    const char *input_file;
    int required = 1;
    FILE_REDIR_CTX fr_ctx;
    ERRORLOG errorlog;

    // Set the "machine" RAM to all 0's
    memset(&fr_ctx, 0, sizeof(FILE_REDIR_CTX));
    errlog_init(&errorlog);

    required &= get_argument(argc, argv, "-i", &input_file);
    get_argument(argc, argv, "-o", &output_file_name);
    fr_ctx.verbose = get_argument(argc, argv, "-v", NULL);
    get_argument(argc, argv, "-s", &symbol_file_name);

    if(!required) {
        usage(argv[0]);
        exit(1);
    }

    FILE_TARGET *initial_target_context = (FILE_TARGET *)malloc(sizeof(FILE_TARGET));
    if(!initial_target_context) {
        fprintf(stderr, "Out of memory");
        exit(1);
    }
    memset(initial_target_context, 0, sizeof(FILE_TARGET));
    if(output_file_name) {
        initial_target_context->file_name = util_strndup(output_file_name, strlen(output_file_name));
        if(!initial_target_context->file_name) {
            free(initial_target_context);
            fprintf(stderr, "Out of memory");
            exit(1);
        }
    }

    CB_ASSEMBLER_CTX cba_file_ctx = {&fr_ctx, output_byte_at_address, output_redirect_start_file, NULL, file_target_release};
    if(A2_OK != assembler_init(&fr_ctx.as, &errorlog, &cba_file_ctx, initial_target_context)) {
        exit(1);
    }

    symbol_write(&fr_ctx.as, "_asm6502_tool", 13, SYMBOL_VARIABLE, 1);

    if(A2_OK != assembler_assemble(&fr_ctx.as, input_file, 0)) {
        fprintf(stderr, "File %s could not be opened\n", input_file);
        exit(1);
    }

    // Write output to stdout if requited
    if(fr_ctx.verbose) {
        for(int target_index = 0; target_index < fr_ctx.as.targets.items; target_index++) {
            TARGET *target = *ARRAY_GET(&fr_ctx.as.targets, TARGET*, target_index);
            FILE_TARGET *ft = (FILE_TARGET*)target->target_ctx;
            if(ft->file_name) {
                printf("FILE: %s\n", ft->file_name);
            }
            for(int i = 0; i < target->segments.items; i++) {
                SEGMENT *s = *ARRAY_GET(&target->segments, SEGMENT*, i);
                if(s->do_not_emit || s->segment_start_address == s->segment_output_address) {
                    // This skips empty (unnamed) segments
                    continue;
                }
                int new_segment = 1;
                for(int address = s->segment_start_address; address < s->segment_output_address; address++) {
                    uint8_t byte_value = ft->RAM_MAIN[address];
                    if(new_segment || 0 == (address % 16)) {
                        printf("\n%04X: ", address);
                        new_segment = 0;
                    }
                    printf("%02X ", byte_value);
                }
                printf("\n\n");
            }
        }
    }

    // Save targets to file if needed
    for(int target_index = 0; target_index < fr_ctx.as.targets.items; target_index++) {
        TARGET *target = *ARRAY_GET(&fr_ctx.as.targets, TARGET*, target_index);
        write_ram_to_file(target);
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
            save_symbols(fp, fr_ctx.as.root_scope, 0);
            save_segments(fp, &fr_ctx.as.targets);
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
    show_segment_gaps(&fr_ctx.as.targets);

    // Shut down, cleanup and exit
    assembler_shutdown(&fr_ctx.as);
    errlog_shutdown(&errorlog);
    array_free(&fr_ctx.as.targets);
    fprintf(stderr, "\nDone.\n");
    return 0;
}

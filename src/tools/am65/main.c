// am65 - command line 6502-family assembler
// Stefan Wessels, 2026
// This is free and unencumbered software released into the public domain.
//
// Front-end around the shared assembler library used by the emulators and the
// standalone tool. It provides a flat 64K image per output target and writes each
// emitting target to a binary file. A named `.scope name file="..."` opens a
// new target, which is how a single source can build several output files
// (loader + game, overlays, ...).

#include "asm.h"
#include "errorlog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Per-target output: a flat 64K image plus the file it is written to.
typedef struct FILE_TARGET {
    char *file_name;   // output path (NULL => not written, e.g. dest-only scope)
    struct FILE_TARGET *sink; // destination-only scopes emit into their parent target
    int wrote_any;     // set once a byte lands in this target
    uint32_t lo;       // lowest address written (valid when wrote_any)
    uint32_t hi;       // one past highest address written (valid when wrote_any)
    uint8_t ram[65536];
} FILE_TARGET;

static char *dup_str(const char *s, int len) {
    if(!s || len < 0) {
        return NULL;
    }
    char *out = malloc((size_t)len + 1);
    if(out) {
        memcpy(out, s, (size_t)len);
        out[len] = '\0';
    }
    return out;
}

static FILE_TARGET *file_target_new(const char *file, int file_len, FILE_TARGET *sink) {
    FILE_TARGET *ft = calloc(1, sizeof(*ft));
    if(!ft) {
        return NULL;
    }
    if(file && file_len > 0) {
        ft->file_name = dup_str(file, file_len);
        if(!ft->file_name) {
            free(ft);
            return NULL;
        }
    }
    ft->sink = sink ? sink : ft;
    return ft;
}

static void file_target_free(FILE_TARGET *ft) {
    if(!ft) {
        return;
    }
    free(ft->file_name);
    free(ft);
}

// ---------------------------------------------------------------------------
// Assembler callbacks.
static void cli_output_byte(void *target, uint16_t addr, uint8_t val) {
    FILE_TARGET *ft = (FILE_TARGET *)target;
    if(!ft) {
        return;
    }
    ft = ft->sink ? ft->sink : ft;
    ft->ram[addr] = val;
    if(!ft->wrote_any) {
        ft->wrote_any = 1;
        ft->lo = addr;
        ft->hi = (uint32_t)addr + 1;
    } else {
        if(addr < ft->lo) {
            ft->lo = addr;
        }
        if((uint32_t)addr + 1 > ft->hi) {
            ft->hi = (uint32_t)addr + 1;
        }
    }
}

static void *cli_target_open(void *user, const char *name, int name_len,
                             const char *file, int file_len,
                             const char *dest, int dest_len) {
    ASSEMBLER *as = (ASSEMBLER *)user;
    FILE_TARGET *parent = as && as->active_target ?
        (FILE_TARGET *)as->active_target->ctx : NULL;
    (void)name;
    (void)name_len;
    (void)dest;
    (void)dest_len;
    // The standalone assembler interprets file= and deliberately ignores
    // dest=. A destination-only scope therefore inherits its parent's output
    // image rather than silently discarding its bytes.
    return file_target_new(file, file_len, file_len > 0 ? NULL : parent);
}

static void cli_target_release(void *user, void *target) {
    (void)user;
    file_target_free((FILE_TARGET *)target);
}

// ---------------------------------------------------------------------------
// Write a target's emitted bytes to its file, as a contiguous image spanning the
// actual range of addresses the source emitted into (lowest..highest). Interior
// gaps are written as they stand in the flat 64K image (zero unless emitted).
static int write_target_file(TARGET *target, int verbose) {
    FILE_TARGET *ft = (FILE_TARGET *)target->ctx;
    if(!ft || !ft->file_name) {
        return 1;
    }
    if(!ft->wrote_any) {
        if(verbose) {
            fprintf(stderr, "Skipping %s: no bytes emitted\n", ft->file_name);
        }
        return 1;  // nothing emitted; not an error
    }

    FILE *fp = fopen(ft->file_name, "wb");
    if(!fp) {
        fprintf(stderr, "Could not open output file %s for writing\n", ft->file_name);
        return 0;
    }
    size_t length = (size_t)(ft->hi - ft->lo);
    size_t written = fwrite(&ft->ram[ft->lo], 1, length, fp);
    fclose(fp);
    if(written != length) {
        fprintf(stderr, "Short write to %s (%zu of %zu bytes)\n", ft->file_name, written, length);
        return 0;
    }
    if(verbose) {
        fprintf(stderr, "Wrote %s: $%04X-$%04X (%zu bytes)\n",
                ft->file_name, ft->lo, ft->hi, length);
    }
    return 1;
}

static void dump_target_hex(TARGET *target) {
    FILE_TARGET *ft = (FILE_TARGET *)target->ctx;
    if(!ft || ft->sink != ft || !ft->wrote_any) {
        return;
    }
    if(ft->file_name) {
        printf("FILE: %s\n", ft->file_name);
    }
    int fresh = 1;
    for(uint32_t addr = ft->lo; addr < ft->hi; addr++) {
        if(fresh || 0 == (addr % 16)) {
            printf("\n%04X: ", addr);
            fresh = 0;
        }
        printf("%02X ", ft->ram[addr]);
    }
    printf("\n\n");
}

// ---------------------------------------------------------------------------
// Symbol listing.
typedef struct {
    FILE *fp;
} symbol_dump_ctx;

static void dump_symbol(const char *name, uint16_t address, void *user) {
    symbol_dump_ctx *ctx = (symbol_dump_ctx *)user;
    fprintf(ctx->fp, "%04X  %s\n", address, name);
}

static void dump_segments(FILE *fp, AM65_DYNARRAY *targets) {
    for(size_t ti = 0; ti < targets->items; ti++) {
        TARGET *t = *AM65_ARRAY_GET(targets, TARGET *, ti);
        FILE_TARGET *ft = (FILE_TARGET *)t->ctx;
        fprintf(fp, "\nSEGMENTS%s%s {",
                (ft && ft->file_name) ? " -> " : "",
                (ft && ft->file_name) ? ft->file_name : "");
        for(size_t si = 0; si < t->segments.items; si++) {
            SEGMENT *s = *AM65_ARRAY_GET(&t->segments, SEGMENT *, si);
            if(s->segment_output_address == s->segment_start_address) {
                continue;
            }
            fprintf(fp, "\n  %.*s [%04X-%04X)%s%s",
                    (int)s->segment_name_length,
                    s->segment_name ? s->segment_name : "",
                    s->segment_start_address, s->segment_output_address,
                    s->do_not_emit ? " (noemit)" : "",
                    s->is_locked ? " (locked)" : "");
        }
        fprintf(fp, "\n}\n");
    }
}

typedef struct {
    FILE *fp;
    int printed_header;
} adjustment_print_ctx;

static void print_segment_adjustment(
    size_t target_index,
    const char *segment_name,
    uint16_t address,
    void *user) {
    adjustment_print_ctx *ctx = (adjustment_print_ctx *)user;
    const char *name = segment_name && segment_name[0] ?
        segment_name : "<default>";
    if(!ctx->printed_header) {
        fprintf(
            ctx->fp,
            "\nAssembly succeeded with adjusted segment addresses; update the source to:\n");
        ctx->printed_header = 1;
    }
    if(target_index == 0) {
        fprintf(ctx->fp, "  Suggest \"%s\" at $%04X\n", name, address);
    } else {
        fprintf(
            ctx->fp,
            "  Target %zu: Suggest \"%s\" at $%04X\n",
            target_index,
            name,
            address);
    }
}

// ---------------------------------------------------------------------------
// Argument handling.
static const char *arg_value(int argc, char **argv, const char *key) {
    for(int i = 1; i < argc - 1; i++) {
        if(0 == strcmp(argv[i], key)) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int arg_flag(int argc, char **argv, const char *key) {
    for(int i = 1; i < argc; i++) {
        if(0 == strcmp(argv[i], key)) {
            return 1;
        }
    }
    return 0;
}

static int parse_uint16(const char *text, uint16_t *out) {
    if(!text || !*text) {
        return 0;
    }
    int base = 10;
    if(text[0] == '$') {
        base = 16;
        text++;
    } else if(text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    char *end = NULL;
    unsigned long value = strtoul(text, &end, base);
    if(!end || *end != '\0' || value > 0xFFFF) {
        return 0;
    }
    *out = (uint16_t)value;
    return 1;
}

static int parse_cpu_profile(const char *text, assembler_cpu_profile *out) {
    if(!text || !out) {
        return 0;
    }
    if(0 == strcmp(text, "6502")) {
        *out = ASM_CPU_6502;
    } else if(0 == strcmp(text, "65c02") || 0 == strcmp(text, "65C02")) {
        *out = ASM_CPU_65C02;
    } else if(0 == strcmp(text, "rockwell")) {
        *out = ASM_CPU_ROCKWELL;
    } else if(0 == strcmp(text, "wdc")) {
        *out = ASM_CPU_WDC;
    } else {
        return 0;
    }
    return 1;
}

static void usage(const char *program) {
    const char *base = program;
    for(const char *c = program; *c; c++) {
        if(*c == '/' || *c == '\\') {
            base = c + 1;
        }
    }
    fprintf(stderr,
        "Usage: %s -i <infile> [-o <outfile>] [-a <addr>] [-s <symfile|->]\n"
        "               [-C <6502|65c02|rockwell|wdc>] [-D name[=value]]...\n"
        "               [--auto-adjust-segments] [-v] [-h]\n"
        "\n"
        "  -i <infile>        assembly source to assemble (required)\n"
        "  -o <outfile>       binary output for the default (unnamed) target\n"
        "  -a <addr>          origin/load address of the default target (default $0000;\n"
        "                     accepts $hex, 0xhex or decimal)\n"
        "  -s <symfile|->     write a symbol + segment listing ('-' = stdout)\n"
        "  -C, --cpu <name>   initial CPU profile (default 6502); source may switch\n"
        "                     profiles with .6502, .65c02, .rockwell, or .wdc\n"
        "  -D name[=value]    predefine a text define (value defaults to \"1\"); repeatable\n"
        "  -A, --auto-adjust-segments\n"
        "                     retry overlapping segment layouts with suggested starts\n"
        "  -v                 verbose: hex-dump each target's output\n"
        "  -h                 show this help\n"
        "\n"
        "A named `.scope name file=\"path\"` inside the source assembles into its own\n"
        "output file, so one source can produce several binaries (loader, overlays...).\n"
        "The standalone tool accepts but ignores dest=; a dest=-only scope continues\n"
        "emitting into its parent output target.\n"
        "The define AM65 is predefined to 1 so source can detect the CLI build with\n"
        "`.if AM65`. No emulator machine symbol is predefined.\n",
        base);
}

static void print_errors(const ERRORLOG *log) {
    if(!log || log->log_array.items == 0) {
        return;
    }
    fprintf(stderr, "\nAssembly errors:\n");
    for(size_t i = 0; i < log->log_array.items; i++) {
        const ERROR_ENTRY *e = AM65_ARRAY_GET((AM65_DYNARRAY *)&log->log_array, ERROR_ENTRY, i);
        if(e && e->err_str) {
            fprintf(stderr, "  %s\n", e->err_str);
        }
    }
}

// A -D flag may add several defines; collect them so we can inject after init.
static int inject_defines(ASSEMBLER *as, int argc, char **argv) {
    for(int i = 1; i < argc - 1; i++) {
        if(0 != strcmp(argv[i], "-D")) {
            continue;
        }
        const char *spec = argv[i + 1];
        const char *eq = strchr(spec, '=');
        if(eq) {
            char *name = dup_str(spec, (int)(eq - spec));
            if(!name || name[0] == '\0') {
                free(name);
                fprintf(stderr, "Invalid -D specification: %s\n", spec);
                return 0;
            }
            int ok = (assembler_predefine(as, name, eq + 1) == ASM_OK);
            free(name);
            if(!ok) {
                fprintf(stderr, "Could not predefine %s\n", spec);
                return 0;
            }
        } else {
            if(assembler_predefine(as, spec, "1") != ASM_OK) {
                fprintf(stderr, "Could not predefine %s\n", spec);
                return 0;
            }
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    if(arg_flag(argc, argv, "-h") || arg_flag(argc, argv, "--help")) {
        usage(argv[0]);
        return 0;
    }

    const char *input_file = arg_value(argc, argv, "-i");
    const char *output_file = arg_value(argc, argv, "-o");
    const char *symbol_file = arg_value(argc, argv, "-s");
    const char *addr_text = arg_value(argc, argv, "-a");
    const char *cpu_text = arg_value(argc, argv, "-C");
    assembler_cpu_profile cpu_profile = ASM_CPU_6502;
    if(!cpu_text) {
        cpu_text = arg_value(argc, argv, "--cpu");
    }
    int verbose = arg_flag(argc, argv, "-v");
    int auto_adjust_segments =
        arg_flag(argc, argv, "-A") ||
        arg_flag(argc, argv, "--auto-adjust-segments");

    if(!input_file) {
        usage(argv[0]);
        return 1;
    }
    if(cpu_text && !parse_cpu_profile(cpu_text, &cpu_profile)) {
        fprintf(stderr, "Invalid CPU profile: %s\n", cpu_text);
        return 1;
    }

    uint16_t origin = 0;
    if(addr_text && !parse_uint16(addr_text, &origin)) {
        fprintf(stderr, "Invalid address: %s\n", addr_text);
        return 1;
    }

    // Default (unnamed) target: its image is written to -o.
    FILE_TARGET *default_target = calloc(1, sizeof(*default_target));
    if(!default_target) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    if(output_file) {
        default_target->file_name = dup_str(output_file, (int)strlen(output_file));
        if(!default_target->file_name) {
            free(default_target);
            fprintf(stderr, "Out of memory\n");
            return 1;
        }
    }
    default_target->sink = default_target;

    ERRORLOG log;
    ASSEMBLER as;
    CB_ASM_CTX cb;
    errlog_init(&log);
    memset(&cb, 0, sizeof(cb));
    cb.user = &as;
    cb.default_target = default_target;
    cb.output_byte = cli_output_byte;
    cb.target_open = cli_target_open;
    cb.target_release = cli_target_release;

    if(assembler_init(&as, &log, &cb) != ASM_OK) {
        fprintf(stderr, "Assembler initialization failed\n");
        file_target_free(default_target);
        errlog_shutdown(&log);
        return 1;
    }

    assembler_set_cpu_profile(&as, cpu_profile);
    // Predefine AM65=1 so source can detect the CLI build, then honour -D flags.
    assembler_predefine(&as, "AM65", "1");
    assembler_set_auto_adjust_segments(&as, auto_adjust_segments);
    if(!inject_defines(&as, argc, argv)) {
        assembler_shutdown(&as);
        file_target_free(default_target);
        errlog_shutdown(&log);
        return 1;
    }

    int result = assembler_assemble(&as, input_file, origin);
    int failed = (result != ASM_OK);

    if(!failed && auto_adjust_segments) {
        adjustment_print_ctx print_ctx = {stderr, 0};
        assembler_walk_segment_adjustments(
            &as,
            print_segment_adjustment,
            &print_ctx);
    }

    if(verbose) {
        for(size_t i = 0; i < as.targets.items; i++) {
            dump_target_hex(*AM65_ARRAY_GET(&as.targets, TARGET *, i));
        }
    }

    if(!failed) {
        for(size_t i = 0; i < as.targets.items; i++) {
            if(!write_target_file(*AM65_ARRAY_GET(&as.targets, TARGET *, i), verbose)) {
                failed = 1;
            }
        }
    }

    if(symbol_file) {
        FILE *fp = (0 == strcmp(symbol_file, "-")) ? stdout : fopen(symbol_file, "w");
        if(!fp) {
            fprintf(stderr, "Could not open symbol file %s for writing\n", symbol_file);
            failed = 1;
        } else {
            symbol_dump_ctx ctx = {fp};
            assembler_walk_symbols(&as, dump_symbol, &ctx);
            dump_segments(fp, &as.targets);
            if(fp != stdout) {
                fclose(fp);
            }
        }
    }

    print_errors(&log);

    assembler_shutdown(&as);
    file_target_free(default_target);
    errlog_shutdown(&log);

    if(failed) {
        fprintf(stderr, "\nAssembly failed.\n");
        return 1;
    }
    return 0;
}

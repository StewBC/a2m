#include "asm.h"
#include "errorlog.h"
#include "../test_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t memory[65536];
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
}

static int write_source(char *path, size_t path_size, const char *source)
{
    return a2m_test_write_temp_file(path, path_size, "a2m_assembler_loops", source);
}

static int assemble_file_at(const char *path, test_memory *mem, ERRORLOG *log, uint16_t start)
{
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;

    if (assembler_init(&as, log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed\n");
        return ASM_ERR;
    }

    result = assembler_assemble(&as, path, start);
    assembler_shutdown(&as);
    return result;
}

static int assemble_file(const char *path, test_memory *mem, ERRORLOG *log)
{
    return assemble_file_at(path, mem, log, 0x0801);
}

static int expect_bytes(const test_memory *mem, const uint8_t *expected, size_t expected_len)
{
    int failures = 0;
    if (memcmp(&mem->memory[0x0801], expected, expected_len) != 0) {
        fprintf(stderr, "loop output mismatch\n");
        failures++;
    }
    for (size_t i = 0; i < expected_len; i++) {
        if (mem->memory[0x0801 + i] != expected[i]) {
            fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                    (size_t)(0x0801 + i), expected[i], mem->memory[0x0801 + i]);
        }
    }
    return failures;
}

static int test_loop_output(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {
        0x00, 0x01, 0x02, 0x03,
        0x10, 0x11, 0x12,
        0x00, 0x01, 0x10, 0x11,
        0xaa, 0xbb
    };
    const char *source =
        ".for i=0, i .lt 4, i++\n"
        "    .byte i\n"
        ".endfor\n"
        ".repeat 3, r\n"
        "    .byte $10 + r\n"
        ".endrep\n"
        ".for outer=0, outer .lt 2, outer++\n"
        "    .repeat 2, inner\n"
        "        .byte outer * 16 + inner\n"
        "    .endrepeat\n"
        ".endfor\n"
        ".repeat 0\n"
        "    .byte $ee\n"
        "    .repeat 1\n"
        "        .byte $ef\n"
        "    .endrepeat\n"
        ".endrepeat\n"
        ".for skipped=0, skipped .lt 0, skipped++\n"
        "    .byte $ff\n"
        ".endfor\n"
        ".byte $aa, $bb\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assembler_assemble failed with %zu errors\n", log.log_array.items);
        failures++;
    }
    failures += expect_bytes(&mem, expected, sizeof(expected));
    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

static int test_loop_errors(void)
{
    struct {
        const char *name;
        const char *source;
    } cases[] = {
        {
            "unmatched endfor",
            ".endfor\n",
        },
        {
            "mismatched nesting",
            ".repeat 1\n"
            ".endfor\n",
        },
        {
            "unclosed loop",
            ".repeat 1\n"
            "    .byte $01\n",
        },
    };
    int failures = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[128];
        test_memory mem;
        ERRORLOG log;

        memset(&mem, 0, sizeof(mem));
        if (write_source(path, sizeof(path), cases[i].source) != 0) {
            failures++;
            continue;
        }

        errlog_init(&log);
        if (assemble_file(path, &mem, &log) != ASM_ERR || log.log_array.items == 0) {
            fprintf(stderr, "%s was not rejected\n", cases[i].name);
            failures++;
        }
        errlog_shutdown(&log);
        a2m_test_remove_file(path);
    }

    return failures;
}

/* Bare "jmp :" is invalid; error text must not be truncated by a NUL from %c. */
static int test_anon_label_bare_colon_error_message(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        "* = $c000\n"
        ": inc $d020\n"
        "  jmp :\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) == ASM_OK || log.log_array.items == 0) {
        fprintf(stderr, "bare anon ':': expected an assembly error\n");
        failures++;
    } else {
        ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, 0);
        if (e->err_str == NULL ||
            (strstr(e->err_str, "'+' or '-'") == NULL &&
             strstr(e->err_str, ":+") == NULL)) {
            fprintf(stderr, "bare anon ':': unclear error: %s\n",
                    e->err_str ? e->err_str : "(null)");
            failures++;
        }
        /* Truncation bug left the message ending at a lone '('. */
        if (e->err_str != NULL) {
            size_t len = strlen(e->err_str);
            if (len > 0 && e->err_str[len - 1] == '(') {
                fprintf(stderr, "bare anon ':': error message truncated at '('\n");
                failures++;
            }
        }
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);
    return failures;
}

static int test_anon_label_with_repeat(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        ".org $2000\n"
        "    lda #81\n"
        "    ldx #40\n"
        ":\n"
        "    .repeat 24, I\n"
        "        sta $0400+I*40,x\n"
        "    .endrep\n"
        "    dex\n"
        "    bne :-\n"
        "    rts\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_OK) {
        fprintf(stderr, "anon label + repeat assembly failed with %zu errors\n", log.log_array.items);
        failures++;
    }

    /* lda #81 = A9 51, ldx #40 = A2 28 */
    if (mem.memory[0x2000] != 0xA9 || mem.memory[0x2001] != 0x51 ||
        mem.memory[0x2002] != 0xA2 || mem.memory[0x2003] != 0x28) {
        fprintf(stderr, "anon label + repeat: preamble mismatch\n");
        failures++;
    }
    /* first sta $0400,x = 9D 00 04 */
    if (mem.memory[0x2004] != 0x9D || mem.memory[0x2005] != 0x00 || mem.memory[0x2006] != 0x04) {
        fprintf(stderr, "anon label + repeat: first sta mismatch: %02X %02X %02X\n",
                mem.memory[0x2004], mem.memory[0x2005], mem.memory[0x2006]);
        failures++;
    }
    /* last sta $0798,x (I=23: $0400+23*40=$0798) = 9D 98 07; ends at $204B */
    if (mem.memory[0x2049] != 0x9D || mem.memory[0x204A] != 0x98 || mem.memory[0x204B] != 0x07) {
        fprintf(stderr, "anon label + repeat: last sta mismatch: %02X %02X %02X\n",
                mem.memory[0x2049], mem.memory[0x204A], mem.memory[0x204B]);
        failures++;
    }
    /* dex = CA, bne :- = D0 B5 (offset -75 to $2004), rts = 60 */
    if (mem.memory[0x204C] != 0xCA || mem.memory[0x204D] != 0xD0 ||
        mem.memory[0x204E] != 0xB5 || mem.memory[0x204F] != 0x60) {
        fprintf(stderr, "anon label + repeat: tail mismatch: %02X %02X %02X %02X\n",
                mem.memory[0x204C], mem.memory[0x204D], mem.memory[0x204E], mem.memory[0x204F]);
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);
    return failures;
}

/* Assembler started at $8000 but source begins with .org $2000 — previously
   dot_org rejected the lower address; now it must succeed. */
static int test_org_below_start(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        ".org $2000\n"
        "    lda #81\n"
        "    ldx #40\n"
        ":\n"
        "    .repeat 24, I\n"
        "        sta $0400+I*40,x\n"
        "    .endrep\n"
        "    dex\n"
        "    bne :-\n"
        "    rts\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file_at(path, &mem, &log, 0x8000) != ASM_OK) {
        fprintf(stderr, "org-below-start failed with %zu errors\n", log.log_array.items);
        for (size_t i = 0; i < log.log_array.items; i++) {
            ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, i);
            fprintf(stderr, "  [%zu] %s\n", i, e->err_str ? e->err_str : "?");
        }
        failures++;
    }
    if (mem.memory[0x2000] != 0xA9 || mem.memory[0x2001] != 0x51) {
        fprintf(stderr, "org-below-start: output at wrong address\n");
        failures++;
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);
    return failures;
}

/* Host default origin $8000 with source * = $0801 and no prior emit must re-anchor. */
static int test_star_below_start(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        "* = $0801\n"
        "    lda #$00\n"
        "    rts\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file_at(path, &mem, &log, 0x8000) != ASM_OK) {
        fprintf(stderr, "star-below-start failed with %zu errors\n", log.log_array.items);
        for (size_t i = 0; i < log.log_array.items; i++) {
            ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, i);
            fprintf(stderr, "  [%zu] %s\n", i, e->err_str ? e->err_str : "?");
        }
        failures++;
    }
    if (mem.memory[0x0801] != 0xA9 || mem.memory[0x0802] != 0x00 || mem.memory[0x0803] != 0x60) {
        fprintf(stderr, "star-below-start: output at wrong address\n");
        failures++;
    }
    /* Going backwards after emit must still fail. */
    {
        const char *after_emit =
            "    nop\n"
            "* = $0801\n"
            "    rts\n";
        errlog_shutdown(&log);
        errlog_init(&log);
        a2m_test_remove_file(path);
        if (write_source(path, sizeof(path), after_emit) != 0) {
            errlog_shutdown(&log);
            return failures + 1;
        }
        if (assemble_file_at(path, &mem, &log, 0x8000) == ASM_OK) {
            fprintf(stderr, "star-below-start: expected error after emit\n");
            failures++;
        }
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);
    return failures;
}

/* Errors should reference files by name relative to the source directory. */
static int test_relative_error_path(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    /* Deliberate error: undefined symbol */
    const char *source = "lda undefined_sym\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    assemble_file(path, &mem, &log);

    if (log.log_array.items == 0) {
        fprintf(stderr, "relative-path: expected at least one error\n");
        errlog_shutdown(&log);
        a2m_test_remove_file(path);
        return 1;
    }

    ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, 0);
    if (e == NULL || e->err_str == NULL) {
        fprintf(stderr, "relative-path: null error entry\n");
        failures++;
    } else {
        /* The error must not contain the directory prefix from path ("/tmp/"). */
        const char *slash = strrchr(path, '/');
        const char *base_name = slash ? slash + 1 : path;
        if (strstr(e->err_str, base_name) == NULL) {
            fprintf(stderr, "relative-path: file name not in error: %s\n", e->err_str);
            failures++;
        }
        /* The error must not start with "File: /tmp/" — only the base name. */
        const char *dir = "/tmp/";
        if (strstr(e->err_str, dir) != NULL) {
            fprintf(stderr, "relative-path: full path leaks into error: %s\n", e->err_str);
            failures++;
        }
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);
    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_loop_output();
    failures += test_loop_errors();
    failures += test_anon_label_bare_colon_error_message();
    failures += test_anon_label_with_repeat();
    failures += test_org_below_start();
    failures += test_star_below_start();
    failures += test_relative_error_path();

    return failures == 0 ? 0 : 1;
}

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
    return a2m_test_write_temp_file(path, path_size, "a2m_assembler_macros", source);
}

static int assemble_file(const char *path, test_memory *mem, ERRORLOG *log)
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

    result = assembler_assemble(&as, path, 0x0801);
    assembler_shutdown(&as);
    return result;
}

static int expect_bytes(const test_memory *mem, const uint8_t *expected, size_t expected_len)
{
    int failures = 0;
    if (memcmp(&mem->memory[0x0801], expected, expected_len) != 0) {
        fprintf(stderr, "macro output mismatch\n");
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

static int test_macro_output(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {
        0xa9, 0x12, 0x8d, 0x20, 0xd0,
        0xb1, 0x50, 0x8d, 0x00, 0x04,
        0x33, 0xee,
        0xa9, 0x01, 0xd0, 0x02, 0xa9, 0xff, 0x01,
        0xa9, 0x02, 0xd0, 0x02, 0xa9, 0xff, 0x02
    };
    const char *source =
        ".macro load_store value dest\n"
        "    lda #value\n"
        "    sta dest\n"
        ".endmacro\n"
        ".macro copy src, dst\n"
        "    lda src\n"
        "    sta dst\n"
        ".endmacro\n"
        ".macro maybe value\n"
        "    .if .defined value\n"
        "        .byte value\n"
        "    .else\n"
        "        .byte $ee\n"
        "    .endif\n"
        ".endmacro\n"
        ".macro branchy value\n"
        "    .local done\n"
        "    lda #value\n"
        "    bne done\n"
        "    lda #$ff\n"
        "done:\n"
        "    .byte value\n"
        ".endmacro\n"
        "load_store $12, $d020\n"
        "copy \"($50),y\", $0400\n"
        "maybe $33\n"
        "maybe\n"
        "branchy $01\n"
        "branchy $02\n";
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

static int test_macro_errors(void)
{
    struct {
        const char *name;
        const char *source;
    } cases[] = {
        {
            "top-level local",
            ".local temp\n",
        },
        {
            "top-level endmacro",
            ".endmacro\n",
        },
        {
            "direct recursion",
            ".macro recurse\n"
            "    recurse\n"
            ".endmacro\n"
            "recurse\n",
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

int main(void)
{
    int failures = 0;

    failures += test_macro_output();
    failures += test_macro_errors();

    return failures == 0 ? 0 : 1;
}

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
    return a2m_test_write_temp_file(path, path_size, "a2m_assembler_conditionals", source);
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

static int test_conditional_output(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const uint8_t expected[] = {0x11, 0x44, 0x88, 0x99, 0xbb, 0xdd};
    const char *source =
        ".if 1\n"
        "    .byte $11\n"
        ".else\n"
        "    .byte $22\n"
        ".endif\n"
        ".if 0\n"
        "    .byte $33\n"
        ".else\n"
        "    .byte $44\n"
        ".endif\n"
        ".if 0\n"
        "    .if 1\n"
        "        .byte $55\n"
        "    .else\n"
        "        .byte $66\n"
        "    .endif\n"
        "    .byte $77\n"
        ".endif\n"
        ".byte $88\n"
        ".if 2 .gt 1\n"
        "    .byte $99\n"
        ".endif\n"
        ".if 1 .eq 0\n"
        "    .byte $aa\n"
        ".endif\n"
        ".if .defined SOME_PARAM\n"
        "    .byte $bb\n"
        ".endif\n"
        ".if .defined\n"
        "    .byte $cc\n"
        ".else\n"
        "    .byte $dd\n"
        ".endif\n";
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

    if (memcmp(&mem.memory[0x0801], expected, sizeof(expected)) != 0) {
        fprintf(stderr, "conditional output mismatch\n");
        failures++;
    }
    for (size_t i = 0; i < sizeof(expected); i++) {
        if (mem.memory[0x0801 + i] != expected[i]) {
            fprintf(stderr, "  $%04zx: expected $%02x, got $%02x\n",
                    (size_t)(0x0801 + i), expected[i], mem.memory[0x0801 + i]);
        }
    }

    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

static int test_forward_if_rejected(void)
{
    char path[128];
    test_memory mem;
    ERRORLOG log;
    const char *source =
        ".if later_label\n"
        "    .byte $ee\n"
        ".endif\n"
        "later_label:\n"
        "    .byte $ff\n";
    int failures = 0;

    memset(&mem, 0, sizeof(mem));
    if (write_source(path, sizeof(path), source) != 0) {
        return 1;
    }

    errlog_init(&log);
    if (assemble_file(path, &mem, &log) != ASM_ERR || log.log_array.items == 0) {
        fprintf(stderr, "forward .if expression was not rejected\n");
        failures++;
    }
    errlog_shutdown(&log);
    a2m_test_remove_file(path);

    return failures;
}

int main(void)
{
    int failures = 0;

    failures += test_conditional_output();
    failures += test_forward_if_rejected();

    return failures == 0 ? 0 : 1;
}

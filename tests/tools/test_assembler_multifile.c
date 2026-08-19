#include "asm.h"
#include "errorlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef A2M_SAMPLE_DIR
#define A2M_SAMPLE_DIR "tests/fixtures/asm"
#endif

typedef struct {
    uint8_t memory[65536];
    int wrote;
} test_memory;

static void output_byte(void *user, uint16_t addr, uint8_t val)
{
    test_memory *mem = (test_memory *)user;
    mem->memory[addr] = val;
    mem->wrote = 1;
}

int main(void)
{
    char path[512];
    test_memory mem;
    ERRORLOG log;
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    snprintf(path, sizeof(path), "%s/main.asm", A2M_SAMPLE_DIR);

    memset(&mem, 0, sizeof(mem));
    memset(&cb, 0, sizeof(cb));
    cb.user = &mem;
    cb.output_byte = output_byte;

    errlog_init(&log);
    if (assembler_init(&as, &log, &cb) != ASM_OK) {
        fprintf(stderr, "assembler_init failed\n");
        return 1;
    }
    assembler_set_cpu_profile(&as, ASM_CPU_65C02);
    result = assembler_assemble(&as, path, 0x6000);
    if (result != ASM_OK) {
        fprintf(stderr, "assemble multi-file failed (%zu errors)\n", log.log_array.items);
        for (size_t i = 0; i < log.log_array.items; i++) {
            ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, i);
            if (e && e->err_str) {
                fprintf(stderr, "  %s\n", e->err_str);
            }
        }
        assembler_shutdown(&as);
        errlog_shutdown(&log);
        return 1;
    }

    /* start: lda #MSG_LEN -> A9 0C */
    if (mem.memory[0x6000] != 0xA9 || mem.memory[0x6001] != 12) {
        fprintf(
            stderr,
            "FAIL: expected lda #12 at $6000, got %02X %02X\n",
            mem.memory[0x6000],
            mem.memory[0x6001]);
        assembler_shutdown(&as);
        errlog_shutdown(&log);
        return 1;
    }
    /* jsr print_len */
    if (mem.memory[0x6002] != 0x20) {
        fprintf(stderr, "FAIL: expected jsr at $6002\n");
        assembler_shutdown(&as);
        errlog_shutdown(&log);
        return 1;
    }
    /* stz $00 */
    if (mem.memory[0x6005] != 0x64 || mem.memory[0x6006] != 0x00) {
        fprintf(
            stderr,
            "FAIL: expected stz $00 at $6005, got %02X %02X\n",
            mem.memory[0x6005],
            mem.memory[0x6006]);
        assembler_shutdown(&as);
        errlog_shutdown(&log);
        return 1;
    }

    assembler_shutdown(&as);
    errlog_shutdown(&log);
    printf("ok\n");
    return 0;
}

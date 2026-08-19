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

static int assemble_source(const char *source, test_memory *mem, ERRORLOG *log)
{
    char path[128];
    CB_ASM_CTX cb;
    ASSEMBLER as;
    int result;

    if (a2m_test_write_temp_file(path, sizeof(path), "a2m_65c02", source) != 0) {
        return ASM_ERR;
    }

    memset(&cb, 0, sizeof(cb));
    cb.user = mem;
    cb.output_byte = output_byte;
    memset(mem, 0, sizeof(*mem));

    if (assembler_init(&as, log, &cb) != ASM_OK) {
        a2m_test_remove_file(path);
        return ASM_ERR;
    }
    result = assembler_assemble(&as, path, 0x2000);
    assembler_shutdown(&as);
    a2m_test_remove_file(path);
    return result;
}

static int expect_bytes(
    const char *name,
    const test_memory *mem,
    uint16_t addr,
    const uint8_t *expected,
    size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (mem->memory[addr + i] != expected[i]) {
            fprintf(
                stderr,
                "FAIL %s @ $%04X+%zu: got %02X expected %02X\n",
                name,
                addr,
                i,
                mem->memory[addr + i],
                expected[i]);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    test_memory mem;
    ERRORLOG log;
    int failures = 0;
    const uint8_t want[] = {
        0x80, 0xFE,       /* bra *  (rel -2) */
        0xDA,             /* phx */
        0x5A,             /* phy */
        0xFA,             /* plx */
        0x7A,             /* ply */
        0x64, 0x10,       /* stz $10 */
        0x9C, 0x00, 0x30, /* stz $3000 */
        0x14, 0x20,       /* trb $20 */
        0x0C, 0x00, 0x40, /* tsb $4000 */
        0x1A,             /* ina / inc a */
        0x3A,             /* dea / dec a */
        0xB2, 0x80,       /* lda ($80) */
    };
    const char *source =
        ".65c02\n"
        "* = $2000\n"
        "    bra *\n"
        "    phx\n"
        "    phy\n"
        "    plx\n"
        "    ply\n"
        "    stz $10\n"
        "    stz $3000\n"
        "    trb $20\n"
        "    tsb $4000\n"
        "    ina\n"
        "    dea\n"
        "    lda ($80)\n";

    errlog_init(&log);
    if (assemble_source(source, &mem, &log) != ASM_OK) {
        fprintf(stderr, "assemble failed (%zu errors)\n", log.log_array.items);
        for (size_t i = 0; i < log.log_array.items; i++) {
            ERROR_ENTRY *e = AM65_ARRAY_GET(&log.log_array, ERROR_ENTRY, i);
            if (e && e->err_str) {
                fprintf(stderr, "  %s\n", e->err_str);
            }
        }
        failures++;
    } else {
        failures += expect_bytes("65c02 ops", &mem, 0x2000, want, sizeof(want));
    }

    /* Reject 65c02-only under .6502 */
    {
        const char *nmos =
            ".6502\n"
            "* = $2000\n"
            "    bra *\n";
        errlog_clean(&log);
        if (assemble_source(nmos, &mem, &log) == ASM_OK) {
            fprintf(stderr, "FAIL: bra should fail under .6502\n");
            failures++;
        }
    }

    /* The standalone/library default is the smallest portable set: 6502. */
    {
        const char *default_cpu =
            "* = $2000\n"
            "    phx\n";
        errlog_clean(&log);
        if (assemble_source(default_cpu, &mem, &log) == ASM_OK) {
            fprintf(stderr, "FAIL: phx should fail under the default 6502 profile\n");
            failures++;
        }
    }

    /* Rockwell adds the numbered bit operations and bit branches. */
    {
        const uint8_t rockwell_want[] = {
            0x07, 0x10,       /* rmb0 $10 */
            0xF7, 0x11,       /* smb7 $11 */
            0x3F, 0x12, 0xF9, /* bbr3 $12,start */
            0xEF, 0x13, 0xF6, /* bbs6 $13,start */
        };
        const char *rockwell =
            ".rockwell\n"
            "* = $2000\n"
            "start:\n"
            "    rmb0 $10\n"
            "    smb7 $11\n"
            "    bbr3 $12,start\n"
            "    bbs6 $13,start\n";
        errlog_clean(&log);
        if (assemble_source(rockwell, &mem, &log) != ASM_OK) {
            fprintf(stderr, "FAIL: Rockwell source did not assemble (%zu errors)\n",
                    log.log_array.items);
            failures++;
        } else {
            failures += expect_bytes(
                "rockwell ops", &mem, 0x2000, rockwell_want, sizeof(rockwell_want));
        }
    }

    /* WDC is the superset: Rockwell bit operations plus WAI and STP. */
    {
        const uint8_t wdc_want[] = {0xCB, 0xDB, 0x27, 0x44};
        const char *wdc =
            ".wdc\n"
            "* = $2000\n"
            "    wai\n"
            "    stp\n"
            "    rmb2 $44\n";
        errlog_clean(&log);
        if (assemble_source(wdc, &mem, &log) != ASM_OK) {
            fprintf(stderr, "FAIL: WDC source did not assemble (%zu errors)\n",
                    log.log_array.items);
            failures++;
        } else {
            failures += expect_bytes("wdc ops", &mem, 0x2000, wdc_want, sizeof(wdc_want));
        }
    }

    /* The profiles are deliberately distinct, not aliases. */
    {
        const char *not_rockwell =
            ".65c02\n"
            "* = $2000\n"
            "    rmb0 $10\n";
        const char *not_wdc =
            ".rockwell\n"
            "* = $2000\n"
            "    wai\n";
        errlog_clean(&log);
        if (assemble_source(not_rockwell, &mem, &log) == ASM_OK) {
            fprintf(stderr, "FAIL: rmb0 should fail under .65c02\n");
            failures++;
        }
        errlog_clean(&log);
        if (assemble_source(not_wdc, &mem, &log) == ASM_OK) {
            fprintf(stderr, "FAIL: wai should fail under .rockwell\n");
            failures++;
        }
    }

    errlog_shutdown(&log);
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}

#include "disasm_pc_lock.h"

#include <stdio.h>
#include <string.h>

static disasm_pc_lock_cache g_cache;
static disasm_pc_lock_line g_lines[DISASM_PC_LOCK_MAX_ROWS];

static void fill_nops(void)
{
    memset(g_cache.bytes, 0xEA, sizeof(g_cache.bytes));
    memset(g_cache.valid, 1, sizeof(g_cache.valid));
}

static int expect_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", label);
        return 1;
    }
    return 0;
}

static int check_pc_lock(uint16_t pc, uint8_t rows, const char *label)
{
    uint8_t pc_row;
    uint8_t k;
    uint16_t top = 0xFFFF;
    symbol_resolver symbols;

    symbol_resolver_null(&symbols);
    memset(g_lines, 0, sizeof(g_lines));
    disasm_pc_lock_build(&g_cache, &symbols, pc, rows, g_lines, &top);

    pc_row = rows / 2u;
    if (g_lines[pc_row].base.address != pc) {
        fprintf(stderr, "%s: pc_row address $%04X, expected $%04X\n",
            label, g_lines[pc_row].base.address, pc);
        return 1;
    }
    if (g_lines[0].base.address != top) {
        fprintf(stderr, "%s: top_address $%04X != lines[0] $%04X\n",
            label, top, g_lines[0].base.address);
        return 1;
    }
    for (k = 0; k < pc_row; ++k) {
        uint16_t addr = g_lines[k].base.address;
        uint8_t len = g_lines[k].base.length;
        if (len == 0 || !disasm_pc_lock_ends_at_or_before(addr, len, pc)) {
            fprintf(stderr, "%s: pre-PC row %u $%04X len %u overlaps PC $%04X\n",
                label, k, addr, len, pc);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    int failures = 0;
    uint8_t fetched[3];
    size_t n;

    /* The debug abort used (uint16)(addr+len) <= pc, which is false for a
     * wrapped 1-byte line at $FFFE when pc=$0001. */
    failures += expect_true(
        ((uint16_t)(0xFFFEu + 1u) <= (uint16_t)0x0001) == 0,
        "old uint16 sum compare fails at wrap");
    failures += expect_true(
        disasm_pc_lock_ends_at_or_before(0xFFFE, 1, 0x0001),
        "1-byte $FFFE is before PC $0001");
    failures += expect_true(
        disasm_pc_lock_ends_at_or_before(0xFFFE, 3, 0x0001),
        "3-byte $FFFE..$0000 ends at PC $0001");
    failures += expect_true(
        !disasm_pc_lock_ends_at_or_before(0xFFFE, 3, 0x0000),
        "3-byte $FFFE..$0000 contains PC $0000");
    failures += expect_true(
        !disasm_pc_lock_ends_at_or_before(0x000E, 3, 0x0010),
        "3-byte $000E overlaps PC $0010");
    failures += expect_true(
        disasm_pc_lock_ends_at_or_before(0x000E, 2, 0x0010),
        "2-byte $000E ends at PC $0010");

    fill_nops();
    g_cache.bytes[0xFFFE] = 0x4C;
    g_cache.bytes[0xFFFF] = 0x34;
    g_cache.bytes[0x0000] = 0x12;
    n = disasm_pc_lock_fetch(&g_cache, 0xFFFE, fetched);
    failures += expect_true(n == 3 && fetched[0] == 0x4C &&
        fetched[1] == 0x34 && fetched[2] == 0x12,
        "fetch wraps $FFFE/$FFFF/$0000");

    fill_nops();
    failures += check_pc_lock(0x0000, 16, "pc=$0000 nops");
    failures += check_pc_lock(0x0001, 16, "pc=$0001 nops");
    failures += check_pc_lock(0xFFFF, 16, "pc=$FFFF nops");
    failures += check_pc_lock(0x0800, 16, "pc=$0800 nops");
    failures += check_pc_lock(0x0000, 40, "pc=$0000 tall");
    failures += check_pc_lock(0x0005, 64, "pc=$0005 64 rows");

    /* JMP $1234 occupying $FFFE..$0000, PC at $0001. */
    fill_nops();
    g_cache.bytes[0xFFFE] = 0x4C;
    g_cache.bytes[0xFFFF] = 0x34;
    g_cache.bytes[0x0000] = 0x12;
    failures += check_pc_lock(0x0001, 16, "pc=$0001 jmp wrap");
    {
        uint8_t pc_row = 16u / 2u;
        uint8_t k;
        int found_jmp = 0;
        for (k = 0; k < pc_row; ++k) {
            if (g_lines[k].base.address == 0xFFFEu &&
                g_lines[k].base.length == 3u) {
                found_jmp = 1;
                break;
            }
        }
        failures += expect_true(found_jmp, "pre-PC includes wrapped JMP at $FFFE");
        failures += expect_true(
            g_lines[pc_row].base.address == 0x0001,
            "pc_row stays on $0001 after wrapped JMP");
    }

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}

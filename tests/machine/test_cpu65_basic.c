#include "apple2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_u8(const char *name, uint8_t expected, uint8_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %02x, got %02x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t expected, uint16_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %04x, got %04x\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %llu, got %llu\n", name,
                (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

/* ROM owns $FFFC; synthetic tests set PC directly in RAM-backed code. */
static void setup_entry(apple2_t *m, uint16_t entry)
{
    m->cpu.cpu.pc = entry;
    m->cpu.cpu.sp = 0x1ff;
    m->cpu.cpu.A = 0;
    m->cpu.cpu.X = 0;
    m->cpu.cpu.Y = 0;
    m->cpu.cpu.flags = 0x20;
    m->cpu.cpu.I = 1;
    m->instruction_complete = true;
    expect_u16("entry PC", entry, m->cpu.cpu.pc);
}

static void test_lda_sta_imm_zp(void)
{
    apple2_t m;
    const uint8_t prog[] = {
        0xa9, 0x42, /* LDA #$42 */
        0x85, 0x10, /* STA $10 */
        0xa9, 0x00, /* LDA #$00 */
        0xa5, 0x10, /* LDA $10 */
        0x00        /* BRK */
    };
    size_t cycles;

    if (!apple2_init(&m)) {
        fail("init");
    }
    apple2_set_cpu_class(&m, CPU_6502);
    apple2_load(&m, 0x0200, prog, sizeof(prog));
    setup_entry(&m, 0x0200);

    cycles = apple2_step_instruction(&m); /* LDA #$42 */
    expect_u64("LDA imm cycles", 2, cycles);
    expect_u8("A after LDA", 0x42, m.cpu.cpu.A);
    expect_u8("Z clear", 0, m.cpu.cpu.Z);

    cycles = apple2_step_instruction(&m); /* STA $10 */
    expect_u64("STA zp cycles", 3, cycles);
    expect_u8("mem $10", 0x42, apple2_debug_read(&m, 0x0010));

    (void)apple2_step_instruction(&m); /* LDA #$00 */
    cycles = apple2_step_instruction(&m); /* LDA $10 */
    expect_u64("LDA zp cycles", 3, cycles);
    expect_u8("A reloaded", 0x42, m.cpu.cpu.A);

    apple2_shutdown(&m);
}

static void test_jsr_rts(void)
{
    apple2_t m;
    /* $0200: JSR $0300 / $0203: LDA #$99 / BRK
       $0300: LDA #$55 / RTS */
    const uint8_t main_prog[] = {
        0x20, 0x00, 0x03, /* JSR $0300 */
        0xa9, 0x99,       /* LDA #$99 */
        0x00
    };
    const uint8_t sub_prog[] = {
        0xa9, 0x55, /* LDA #$55 */
        0x60        /* RTS */
    };

    if (!apple2_init(&m)) {
        fail("init");
    }
    apple2_set_cpu_class(&m, CPU_6502);
    apple2_load(&m, 0x0200, main_prog, sizeof(main_prog));
    apple2_load(&m, 0x0300, sub_prog, sizeof(sub_prog));
    setup_entry(&m, 0x0200);

    expect_u64("JSR cycles", 6, apple2_step_instruction(&m));
    expect_u16("PC in sub", 0x0300, m.cpu.cpu.pc);

    expect_u64("LDA in sub", 2, apple2_step_instruction(&m));
    expect_u8("A from sub", 0x55, m.cpu.cpu.A);

    expect_u64("RTS cycles", 6, apple2_step_instruction(&m));
    expect_u16("PC after RTS", 0x0203, m.cpu.cpu.pc);

    expect_u64("LDA after RTS", 2, apple2_step_instruction(&m));
    expect_u8("A overwritten", 0x99, m.cpu.cpu.A);

    apple2_shutdown(&m);
}

static void test_step_cycle_matches_instruction(void)
{
    apple2_t a;
    apple2_t b;
    const uint8_t prog[] = {
        0xa9, 0x01, /* LDA #1 */
        0x69, 0x02, /* ADC #2 */
        0x85, 0x20, /* STA $20 */
        0xe8,       /* INX */
        0x00
    };
    size_t i;

    if (!apple2_init(&a) || !apple2_init(&b)) {
        fail("init pair");
    }
    apple2_set_cpu_class(&a, CPU_6502);
    apple2_set_cpu_class(&b, CPU_6502);
    apple2_load(&a, 0x1000, prog, sizeof(prog));
    apple2_load(&b, 0x1000, prog, sizeof(prog));
    setup_entry(&a, 0x1000);
    setup_entry(&b, 0x1000);

    /* Run 4 instructions via step_instruction on A. */
    for (i = 0; i < 4; i++) {
        (void)apple2_step_instruction(&a);
    }

    /* Run the same wall of cycles via step_cycle on B. */
    while (apple2_cycles(&b) < apple2_cycles(&a)) {
        if (!apple2_step_cycle(&b)) {
            fail("step_cycle");
        }
    }

    expect_u64("cycle match", apple2_cycles(&a), apple2_cycles(&b));
    expect_u8("A match", a.cpu.cpu.A, b.cpu.cpu.A);
    expect_u8("X match", a.cpu.cpu.X, b.cpu.cpu.X);
    expect_u16("PC match", a.cpu.cpu.pc, b.cpu.cpu.pc);
    expect_u8("mem match", apple2_debug_read(&a, 0x0020), apple2_debug_read(&b, 0x0020));
    expect_u8("result 3", 0x03, a.cpu.cpu.A);
    expect_u8("X is 1", 0x01, a.cpu.cpu.X);

    apple2_shutdown(&a);
    apple2_shutdown(&b);
}

static void test_branch_taken_cycles(void)
{
    apple2_t m;
    /* LDA #0 / BEQ +2 / NOP / LDA #$FF / BRK  — branch stays on page */
    const uint8_t prog[] = {
        0xa9, 0x00, /* LDA #0 */
        0xf0, 0x01, /* BEQ +1 -> skip NOP */
        0xea,       /* NOP (skipped) */
        0xa9, 0xff, /* LDA #$FF */
        0x00
    };
    size_t cycles;

    if (!apple2_init(&m)) {
        fail("init");
    }
    apple2_set_cpu_class(&m, CPU_6502);
    apple2_load(&m, 0x0400, prog, sizeof(prog));
    setup_entry(&m, 0x0400);

    (void)apple2_step_instruction(&m); /* LDA */
    cycles = apple2_step_instruction(&m); /* BEQ taken, same page = 3 */
    expect_u64("BEQ taken same page", 3, cycles);
    expect_u16("PC after branch", 0x0405, m.cpu.cpu.pc);

    apple2_shutdown(&m);
}

int main(void)
{
    test_lda_sta_imm_zp();
    test_jsr_rts();
    test_step_cycle_matches_instruction();
    test_branch_taken_cycles();
    printf("cpu65_basic: all tests passed\n");
    return 0;
}

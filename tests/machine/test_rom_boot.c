#include "apple2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

/* Scan text page 1 ($400-$7FF) for an ASCII substring (ignoring high bit). */
static int text_page_contains(const apple2_t *m, const char *needle)
{
    char buf[0x400 + 1];
    size_t i;
    size_t n = strlen(needle);

    for (i = 0; i < 0x400; i++) {
        buf[i] = (char)(apple2_debug_read(m, (uint16_t)(0x0400 + i)) & 0x7F);
    }
    buf[0x400] = '\0';
    for (i = 0; i + n <= 0x400; i++) {
        if (memcmp(buf + i, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static void run_until(apple2_t *m, uint64_t max_cycles, const char *needle)
{
    uint64_t start = apple2_cycles(m);

    while (apple2_cycles(m) - start < max_cycles) {
        if (!apple2_step_cycles(m, 1000, NULL)) {
            fail("step_cycles");
        }
        if (text_page_contains(m, needle)) {
            return;
        }
    }
    fprintf(stderr, "FAIL: did not find \"%s\" within %llu cycles (pc=%04x)\n",
            needle, (unsigned long long)max_cycles, m->cpu.cpu.pc);
    exit(1);
}

static void test_iie_boot_basic(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }
    if (m.model != APPLE2_MODEL_IIE_ENHANCED) {
        fail("model");
    }
    if (m.cpu.cpu.class != CPU_65c02) {
        fail("cpu class");
    }
    if (m.cpu.cpu.pc < 0xC000) {
        fprintf(stderr, "FAIL: unexpected reset PC %04x\n", m.cpu.cpu.pc);
        exit(1);
    }

    /* Banner is mixed-case "Apple //e" then BASIC "]" prompt. */
    run_until(&m, 8000000ull, "Apple");
    apple2_shutdown(&m);
    printf("rom_boot: //e reached text containing Apple\n");
}

static void test_iiplus_boot_class(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }
    apple2_set_model(&m, APPLE2_MODEL_II_PLUS);
    if (m.cpu.cpu.class != CPU_6502) {
        fail("][+ should be 6502");
    }
    if (m.cpu.cpu.pc < 0xC000) {
        fprintf(stderr, "FAIL: ][+ reset PC %04x\n", m.cpu.cpu.pc);
        exit(1);
    }

    run_until(&m, 8000000ull, "APPLE"); /* ][+ Autostart uses uppercase */
    apple2_shutdown(&m);
    printf("rom_boot: ][+ reached text containing APPLE\n");
}

int main(void)
{
    test_iie_boot_basic();
    test_iiplus_boot_class();
    printf("rom_boot: all tests passed\n");
    return 0;
}

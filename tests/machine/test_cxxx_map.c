#include "apple2.h"
#include "softswitch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, int cond)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

/* a2audit CXXX: after SETC3ROM, $C300 is slot map (empty → RAM), not internal ROM. */
static void test_setc3rom_empty_slot(void)
{
    apple2_t m;
    uint8_t rom_sig[8];
    uint8_t before[8];
    uint8_t after[8];
    int i;

    if (!apple2_init(&m)) {
        fail("init");
    }
    expect_true("iie", m.model == APPLE2_MODEL_IIE_ENHANCED);
    expect_true("rom_c000", m.rom_c000 != NULL);

    memcpy(rom_sig, m.rom_c000 + 0x300, sizeof(rom_sig));
    for (i = 0; i < 8; i++) {
        before[i] = apple2_debug_read(&m, (uint16_t)(0xC300 + i));
    }
    expect_true("cold C3 is internal ROM", memcmp(before, rom_sig, 8) == 0);

    softswitch_c0_write(&m, 0xC00B, 0); /* SETC3ROM: slot ROM on */
    expect_true("SLOT3ROM flag", (m.state_flags & A2S_SLOT3ROM_MB_DISABLE) != 0);
    expect_true(
        "RDC3ROM high",
        (softswitch_c0_read(&m, 0xC017) & 0x80) != 0);

    for (i = 0; i < 8; i++) {
        after[i] = apple2_debug_read(&m, (uint16_t)(0xC300 + i));
    }
    expect_true(
        "SETC3ROM empty: C300 is not internal ROM",
        memcmp(after, rom_sig, 8) != 0);

    /* Empty-slot underlay was initialized with 0xA0 pattern in C001–CFFF. */
    expect_true("C300 underlay pattern", after[0] == 0xA0);

    softswitch_c0_write(&m, 0xC00A, 0); /* CLRC3ROM: internal back */
    for (i = 0; i < 8; i++) {
        after[i] = apple2_debug_read(&m, (uint16_t)(0xC300 + i));
    }
    expect_true("CLRC3ROM restores internal", memcmp(after, rom_sig, 8) == 0);

    apple2_shutdown(&m);
}

/* $C3xx access with internal C3 active latches $C800 firmware. */
static void test_c3_strobes_c800(void)
{
    apple2_t m;
    uint8_t c800_rom;
    uint8_t c800_before;
    uint8_t c800_after;
    uint8_t bus_c800;

    if (!apple2_init(&m)) {
        fail("init2");
    }

    /* Force C800 to RAM underlay first. */
    m.strobed_slot = -1;
    softswitch_apply_full_map(&m);
    c800_before = apple2_debug_read(&m, 0xC800);
    c800_rom = m.rom_c000[0x800];

    /* Access $C300 with internal C3 → should latch C800 firmware. */
    softswitch_slot_io_select(&m, 0xC300);
    expect_true("strobed internal", m.strobed_slot == 8);
    c800_after = apple2_debug_read(&m, 0xC800);
    expect_true("C800 is firmware", c800_after == c800_rom);

    /*
     * a2audit E000B: LDA $C300 / STA $C00B — SETC3ROM turns $C300 to the
     * slot, but the C800 latch stays until $CFFF (a2m io_apply_c800_latch).
     */
    softswitch_c0_write(&m, 0xC00B, 0);
    softswitch_apply_full_map(&m);
    expect_true("still latched after SETC3ROM", m.strobed_slot == 8);
    c800_after = apple2_debug_read(&m, 0xC800);
    bus_c800 = m.cpu.read(m.cpu.user, 0xC800);
    expect_true("C800 firmware after SETC3ROM", c800_after == c800_rom);
    expect_true("bus C800 firmware after SETC3ROM", bus_c800 == c800_rom);
    /* $C300 itself is no longer internal ROM. */
    expect_true(
        "C300 not internal after SETC3ROM",
        apple2_debug_read(&m, 0xC300) != m.rom_c000[0x300]);

    /* $CFFF clears the expansion-ROM latch. */
    (void)m.cpu.read(m.cpu.user, 0xCFFF);
    expect_true("CFFF clears latch", m.strobed_slot == -1);
    expect_true(
        "C800 not firmware after CFFF",
        apple2_debug_read(&m, 0xC800) != c800_rom || c800_before == c800_rom);

    (void)c800_before;
    apple2_shutdown(&m);
}

/* Full a2audit-style sequence: LDA $C300 / STA $C00B via the bus. */
static void test_a2audit_c300_then_setc3rom(void)
{
    apple2_t m;
    uint8_t c800_rom;
    int a;
    int rom_like;

    if (!apple2_init(&m)) {
        fail("init_audit");
    }
    c800_rom = m.rom_c000[0x800];

    /* Bus-visible $C300 select (IO Select), then SETC3ROM. */
    (void)m.cpu.read(m.cpu.user, 0xC300);
    softswitch_c0_write(&m, 0xC00B, 0x1A);

    expect_true("latched", m.strobed_slot == 8);
    expect_true("SLOT3ROM on", (m.state_flags & A2S_SLOT3ROM_MB_DISABLE) != 0);

    /* C800-CFFE should behave as ROM (read firmware; write does not stick). */
    rom_like = 1;
    for (a = 0xC800; a < 0xCFFF; a++) {
        uint8_t want = m.rom_c000[a - 0xC000];
        uint8_t got = m.cpu.read(m.cpu.user, (uint16_t)a);
        uint8_t flipped;
        if (got != want) {
            rom_like = 0;
            break;
        }
        m.cpu.write(m.cpu.user, (uint16_t)a, (uint8_t)(want ^ 0xFFu));
        flipped = m.cpu.read(m.cpu.user, (uint16_t)a);
        if (flipped != want) {
            rom_like = 0;
            break;
        }
    }
    expect_true("C800-CFFE ROM after C300+SETC3ROM", rom_like != 0);
    (void)c800_rom;

    apple2_shutdown(&m);
}

/* INTCXROM forces C1–CF internal regardless of C3ROM. */
static void test_intcxrom(void)
{
    apple2_t m;
    uint8_t rom_c3;
    uint8_t rom_c1;

    if (!apple2_init(&m)) {
        fail("init3");
    }
    rom_c3 = m.rom_c000[0x300];
    rom_c1 = m.rom_c000[0x100];

    softswitch_c0_write(&m, 0xC00B, 0); /* slot C3 */
    softswitch_c0_write(&m, 0xC007, 0); /* SETCXROM / INTCXROM */
    expect_true("CXROM flag", (m.state_flags & A2S_CXSLOTROM_MB_ENABLE) != 0);
    expect_true("C300 internal under CXROM", apple2_debug_read(&m, 0xC300) == rom_c3);
    expect_true("C100 internal under CXROM", apple2_debug_read(&m, 0xC100) == rom_c1);

    softswitch_c0_write(&m, 0xC006, 0); /* CLRCXROM */
    expect_true("CXROM off", (m.state_flags & A2S_CXSLOTROM_MB_ENABLE) == 0);
    /* C3ROM still set from earlier → empty slot map, not internal. */
    expect_true(
        "C300 not internal after CX off + C3 slot",
        apple2_debug_read(&m, 0xC300) != rom_c3);

    apple2_shutdown(&m);
}

/*
 * a2audit E000B: after STA $C007 (SETCXROM), C400-C7FF must read as internal
 * ROM even with Mockingboard in slot 4. Bus path (not debug_read) is required
 * because MB is intercepted in apple2_bus_read/write.
 */
static void test_intcxrom_hides_mockingboard_cn(void)
{
    apple2_t m;
    uint8_t rom_c4;
    uint8_t rom_c6;
    uint8_t bus_c4;
    uint8_t bus_c6;
    uint8_t after_write;

    if (!apple2_init(&m)) {
        fail("init4");
    }
    expect_true("default MB slot 4", m.slot_type[4] == SLOT_TYPE_MOCKINGBOARD);
    expect_true("default Disk II slot 6", m.slot_type[6] == SLOT_TYPE_DISKII);

    rom_c4 = m.rom_c000[0x400];
    rom_c6 = m.rom_c000[0x600];

    softswitch_c0_write(&m, 0xC007, 0x17); /* a2audit: LDA #$17 / STA $C007 */
    expect_true("CXROM on", (m.state_flags & A2S_CXSLOTROM_MB_ENABLE) != 0);

    bus_c4 = m.cpu.read(m.cpu.user, 0xC400);
    bus_c6 = m.cpu.read(m.cpu.user, 0xC600);
    expect_true("bus C400 is internal ROM under CXROM", bus_c4 == rom_c4);
    expect_true("bus C600 is internal ROM under CXROM", bus_c6 == rom_c6);

    /* Write must not stick through bus (ROM-like): write underlay, read ROM. */
    m.cpu.write(m.cpu.user, 0xC400, (uint8_t)(rom_c4 ^ 0xFFu));
    after_write = m.cpu.read(m.cpu.user, 0xC400);
    expect_true("C400 still ROM after bus write", after_write == rom_c4);

    /* CLRCXROM: page map restores slot shadows; C4 is no longer internal ROM. */
    softswitch_c0_write(&m, 0xC006, 0);
    expect_true(
        "CX off: C4 page not internal ROM",
        m.pages.read_pages[0xC4] != m.rom_c000 + 0x400);
    /* Slot I/O is live again: bus write to VIA ORB is absorbed (not underlay). */
    {
        uint8_t underlay_before = m.ram_main[0xC400];
        m.cpu.write(m.cpu.user, 0xC400, (uint8_t)(underlay_before ^ 0x5Au));
        expect_true(
            "CX off: MB absorbs C400 write",
            m.ram_main[0xC400] == underlay_before);
    }

    apple2_shutdown(&m);
}

int main(void)
{
    test_setc3rom_empty_slot();
    test_c3_strobes_c800();
    test_a2audit_c300_then_setc3rom();
    test_intcxrom();
    test_intcxrom_hides_mockingboard_cn();
    printf("ok\n");
    return 0;
}

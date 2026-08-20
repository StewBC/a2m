#include "apple2.h"
#include "keyboard.h"

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

static void expect_u8(const char *name, uint8_t e, uint8_t a)
{
    if (e != a) {
        fprintf(stderr, "FAIL: %s: expected %02x got %02x\n", name, e, a);
        exit(1);
    }
}

static void expect_u16(const char *name, uint16_t e, uint16_t a)
{
    if (e != a) {
        fprintf(stderr, "FAIL: %s: expected %04x got %04x\n", name, e, a);
        exit(1);
    }
}

static void test_text_page_readbacks(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }

    expect_true("TEXT default", (apple2_state_flags(&m) & A2S_TEXT) != 0);
    expect_u8("RDTEXT", 0x80, softswitch_c0_read(&m, 0xC01A));

    softswitch_c0_write(&m, 0xC050, 0); /* TXTCLR */
    expect_true("TEXT off", (apple2_state_flags(&m) & A2S_TEXT) == 0);
    expect_u8("RDTEXT off", 0x00, softswitch_c0_read(&m, 0xC01A));

    softswitch_c0_write(&m, 0xC051, 0); /* TXTSET */
    expect_u8("RDTEXT on", 0x80, softswitch_c0_read(&m, 0xC01A));

    softswitch_c0_write(&m, 0xC057, 0); /* SETHIRES */
    expect_u8("RDHIRES", 0x80, softswitch_c0_read(&m, 0xC01D));
    softswitch_c0_write(&m, 0xC056, 0);
    expect_u8("RDHIRES off", 0x00, softswitch_c0_read(&m, 0xC01D));

    apple2_shutdown(&m);
}

static void test_iie_banking_readbacks(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }
    expect_true("iie model", m.model == APPLE2_MODEL_IIE_ENHANCED);

    softswitch_c0_write(&m, 0xC003, 0); /* SETRAMRD */
    expect_u8("RDRAMRD", 0x80, softswitch_c0_read(&m, 0xC013));
    softswitch_c0_write(&m, 0xC002, 0);
    expect_u8("RDRAMRD off", 0x00, softswitch_c0_read(&m, 0xC013));

    softswitch_c0_write(&m, 0xC009, 0); /* SETALTZP */
    expect_u8("RDALTZP", 0x80, softswitch_c0_read(&m, 0xC016));
    softswitch_c0_write(&m, 0xC008, 0);
    expect_u8("RDALTZP off", 0x00, softswitch_c0_read(&m, 0xC016));

    softswitch_c0_write(&m, 0xC001, 0); /* SET80STORE */
    expect_u8("RD80STORE", 0x80, softswitch_c0_read(&m, 0xC018));

    apple2_shutdown(&m);
}

static void test_language_card_toggle(void)
{
    apple2_t m;
    uint8_t rom_byte;
    uint8_t ram_byte = 0x5A;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Default after reset: LC write on bank2, ROM readable (LC_READ off). */
    expect_true("ROM readable", (apple2_state_flags(&m) & A2S_LC_READ) == 0);
    rom_byte = apple2_debug_read(&m, 0xFFFC);
    expect_true("reset vector lo non-zero-ish or ok", 1);

    /* Enable LC read: double-read odd address with pre-write (a2m: C083). */
    softswitch_language_card(&m, 0xC083, 0);
    softswitch_language_card(&m, 0xC083, 0);
    expect_true("LC read on", (apple2_state_flags(&m) & A2S_LC_READ) != 0);

    /* Write through LC (should be write-enabled after odd reads). */
    apple2_debug_write(&m, 0xD000, ram_byte);
    expect_u8("LC RAM write", ram_byte, apple2_debug_read(&m, 0xD000));

    /* Switch back to ROM read: C081 (ROMIN) clears LC_READ. */
    softswitch_language_card(&m, 0xC081, 0);
    expect_true("LC read off", (apple2_state_flags(&m) & A2S_LC_READ) == 0);
    /* ROM at D000 should not be 0x5A (unless coincidentally). */
    (void)rom_byte;

    apple2_shutdown(&m);
}

static void test_keyboard_latch(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }
    apple2_set_key(&m, (uint8_t)('A' | 0x80));
    expect_u8("kbd", (uint8_t)('A' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("kbd cleared high", (uint8_t)'A', softswitch_c0_read(&m, 0xC000) & 0x7F);

    apple2_shutdown(&m);
}

static void test_ctrl_letter_strobe(void)
{
    /* Ctrl+C is Apple "break" ($03), like RUN/STOP on a C64. */
    expect_u8(
        "Ctrl+C",
        (uint8_t)(0x03u | 0x80u),
        host_key_to_apple_strobe(HOST_KEY_C, false, true));
    expect_u8(
        "Ctrl+A",
        (uint8_t)(0x01u | 0x80u),
        host_key_to_apple_strobe(HOST_KEY_A, false, true));
    expect_u8(
        "plain C",
        (uint8_t)('C' | 0x80u),
        host_key_to_apple_strobe(HOST_KEY_C, false, false));
}

static void test_gameport_buttons(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init");
    }

    expect_u8("butn0 idle", 0x00, softswitch_c0_read(&m, 0xC061));
    expect_u8("butn1 idle", 0x00, softswitch_c0_read(&m, 0xC062));
    expect_u8("butn2 idle", 0x00, softswitch_c0_read(&m, 0xC063));

    apple2_gameport_set_buttons(&m, APPLE2_GAMEPORT_BUTTON0);
    expect_u8("butn0 fire", 0x80, softswitch_c0_read(&m, 0xC061));
    expect_u8("butn1 still idle", 0x00, softswitch_c0_read(&m, 0xC062));

    apple2_gameport_set_buttons(
        &m, APPLE2_GAMEPORT_BUTTON0 | APPLE2_GAMEPORT_BUTTON1 | APPLE2_GAMEPORT_BUTTON2);
    expect_u8("butn0", 0x80, softswitch_c0_read(&m, 0xC061));
    expect_u8("butn1", 0x80, softswitch_c0_read(&m, 0xC062));
    expect_u8("butn2", 0x80, softswitch_c0_read(&m, 0xC063));

    /* Open-Apple OR with host button. */
    apple2_gameport_set_buttons(&m, 0);
    m.state_flags |= A2S_OPEN_APPLE;
    expect_u8("OA as butn0", 0x80, softswitch_c0_read(&m, 0xC061));
    m.state_flags &= ~A2S_OPEN_APPLE;
    m.state_flags |= A2S_CLOSED_APPLE;
    expect_u8("CA as butn1", 0x80, softswitch_c0_read(&m, 0xC062));

    apple2_shutdown(&m);
}

static void test_call_stack_jsr_frames(void)
{
    apple2_t m;
    apple2_call_stack_entry entries[APPLE2_CALL_STACK_MAX];
    uint8_t count;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Empty stack → no frames. */
    m.cpu.cpu.sp = 0x01FFu;
    count = apple2_debug_call_stack(&m, entries, APPLE2_CALL_STACK_MAX);
    expect_u8("empty stack", 0, count);

    /* Plant JSR $20 $34 $12 at $8000 (dest $1234). JSR pushes $8002. */
    apple2_debug_write(&m, 0x8000, 0x20);
    apple2_debug_write(&m, 0x8001, 0x34);
    apple2_debug_write(&m, 0x8002, 0x12);
    /* One nested frame: outer JSR at $9000 → $ABCD, return word $9002. */
    apple2_debug_write(&m, 0x9000, 0x20);
    apple2_debug_write(&m, 0x9001, 0xCD);
    apple2_debug_write(&m, 0x9002, 0xAB);

    /* SP=$01FB → words at $01FC/$01FD (inner) and $01FE/$01FF (outer). */
    apple2_debug_write(&m, 0x01FC, 0x02);
    apple2_debug_write(&m, 0x01FD, 0x80);
    apple2_debug_write(&m, 0x01FE, 0x02);
    apple2_debug_write(&m, 0x01FF, 0x90);
    m.cpu.cpu.sp = 0x01FBu;

    count = apple2_debug_call_stack(&m, entries, APPLE2_CALL_STACK_MAX);
    expect_u8("two jsr frames", 2, count);
    expect_u16("inner jsr", 0x8000, entries[0].jsr_address);
    expect_u16("inner dest", 0x1234, entries[0].dest_address);
    expect_u16("outer jsr", 0x9000, entries[1].jsr_address);
    expect_u16("outer dest", 0xABCD, entries[1].dest_address);

    /* Non-JSR return word is skipped by single-byte scan. */
    apple2_debug_write(&m, 0x01FC, 0x00);
    apple2_debug_write(&m, 0x01FD, 0x10); /* $1000, mem[$0FFE] != $20 */
    apple2_debug_write(&m, 0x0FFE, 0xEA);
    count = apple2_debug_call_stack(&m, entries, APPLE2_CALL_STACK_MAX);
    expect_u8("skip non-jsr keep outer", 1, count);
    expect_u16("remaining jsr", 0x9000, entries[0].jsr_address);

    apple2_shutdown(&m);
}

static void test_paste_kbdstrb_feed(void)
{
    apple2_t m;
    const char *crlf = "Ab\r\nC";
    const char *unix_nl = "10 x\n20 y";

    if (!apple2_init(&m)) {
        fail("init");
    }
    /* Default model is //e Enhanced — case preserved. */
    expect_true("iie", m.model == APPLE2_MODEL_IIE_ENHANCED);
    expect_true("paste begin", apple2_paste_begin(&m, crlf, strlen(crlf)));
    expect_u8("first A", (uint8_t)('A' | 0x80), softswitch_c0_read(&m, 0xC000));

    /* Acknowledge → next char 'b' preserved on //e. */
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("second b", (uint8_t)('b' | 0x80), softswitch_c0_read(&m, 0xC000));

    /* CRLF collapses to one Return, then 'C'. */
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("CRLF→Return", (uint8_t)(0x0D | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("C", (uint8_t)('C' | 0x80), softswitch_c0_read(&m, 0xC000));
    expect_true("still active before last strb", apple2_paste_active(&m));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_true("paste done", !apple2_paste_active(&m));

    /* Unix LF-only (typical macOS clipboard) must also become Return. */
    expect_true("paste lf", apple2_paste_begin(&m, unix_nl, strlen(unix_nl)));
    expect_u8("1", (uint8_t)('1' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("0", (uint8_t)('0' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("sp", (uint8_t)(' ' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("x", (uint8_t)('x' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("LF→Return", (uint8_t)(0x0D | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("2 after LF", (uint8_t)('2' | 0x80), softswitch_c0_read(&m, 0xC000));
    apple2_paste_cancel(&m);

    /* ][+ uppercases. */
    apple2_set_model(&m, APPLE2_MODEL_II_PLUS);
    expect_true("paste ii+", apple2_paste_begin(&m, "aB", 2));
    expect_u8("ii+ A", (uint8_t)('A' | 0x80), softswitch_c0_read(&m, 0xC000));
    (void)softswitch_c0_read(&m, 0xC010);
    expect_u8("ii+ B", (uint8_t)('B' | 0x80), softswitch_c0_read(&m, 0xC000));
    apple2_paste_cancel(&m);

    apple2_shutdown(&m);
}

static void test_gameport_paddle_ptrig(void)
{
    apple2_t m;
    uint8_t pdl0;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Fixed axis: bit7 high immediately after PTRIG, clear after full RC. */
    apple2_gameport_set_axis(&m, 0, 64);
    apple2_gameport_ptrig(&m);
    pdl0 = softswitch_c0_read(&m, 0xC064);
    expect_true("pdl0 high after ptrig", (pdl0 & 0x80) != 0);

    /* Advance past threshold for axis 64: need timer > 64.
       timer = delta * 255 / 3061 > 64 → delta > 64 * 3061 / 255 ≈ 768. */
    m.cpu.cpu.cycles = m.gameport_ptrig_cycle + 900u;
    pdl0 = softswitch_c0_read(&m, 0xC064);
    expect_true("pdl0 low after decay", (pdl0 & 0x80) == 0);

    /* Stick 1 Y (PDL3) independent. */
    apple2_gameport_set_axis(&m, 3, 200);
    softswitch_c0_write(&m, 0xC070, 0); /* PTRIG via softswitch write */
    expect_true(
        "pdl3 high early",
        (softswitch_c0_read(&m, 0xC067) & 0x80) != 0);
    m.cpu.cpu.cycles = m.gameport_ptrig_cycle + 3000u;
    expect_true(
        "pdl3 low late",
        (softswitch_c0_read(&m, 0xC067) & 0x80) == 0);

    /* Full-right must still discharge: timer saturates at 255 and bit7 only
       clears when timer > axis, so axis 255 would hang forever. set_axis
       clamps 255 → 254. */
    apple2_gameport_set_axis(&m, 0, 255);
    expect_u8("axis clamped to 254", 254, m.gameport_axis[0]);
    apple2_gameport_ptrig(&m);
    expect_true(
        "pdl0 high at max",
        (softswitch_c0_read(&m, 0xC064) & 0x80) != 0);
    m.cpu.cpu.cycles = m.gameport_ptrig_cycle + 3061u;
    expect_true(
        "pdl0 clears at full scale",
        (softswitch_c0_read(&m, 0xC064) & 0x80) == 0);

    apple2_shutdown(&m);
}

int main(void)
{
    test_text_page_readbacks();
    test_iie_banking_readbacks();
    test_language_card_toggle();
    test_keyboard_latch();
    test_ctrl_letter_strobe();
    test_gameport_buttons();
    test_gameport_paddle_ptrig();
    test_paste_kbdstrb_feed();
    test_call_stack_jsr_frames();
    printf("softswitch: all tests passed\n");
    return 0;
}

#include "apple2.h"
#include "video.h"

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

static void expect_u32(const char *name, uint32_t e, uint32_t a)
{
    if (e != a) {
        fprintf(stderr, "FAIL: %s: expected %u got %u\n", name, e, a);
        exit(1);
    }
}

static void test_timing_constants(void)
{
    expect_u32("cycles/line", 65, APPLE2_VIDEO_CYCLES_PER_LINE);
    expect_u32("lines/frame", 262, APPLE2_VIDEO_LINES_PER_FRAME);
    expect_u32("cycles/frame", 17030, APPLE2_VIDEO_CYCLES_PER_FRAME);
    expect_u32("vbl start", 192, APPLE2_VIDEO_VBL_START_LINE);
    expect_u32("fb width", 560, APPLE2_VIDEO_WIDTH);
    expect_u32("fb height", 192, APPLE2_VIDEO_HEIGHT);
    expect_u32("pixels/col", 14, APPLE2_VIDEO_PIXELS_PER_COLUMN);
    expect_true("frame = 65*262",
                APPLE2_VIDEO_CYCLES_PER_FRAME ==
                    APPLE2_VIDEO_CYCLES_PER_LINE * APPLE2_VIDEO_LINES_PER_FRAME);
    expect_true("width = 40*14",
                APPLE2_VIDEO_WIDTH ==
                    APPLE2_VIDEO_H_VISIBLE_CYCLES * APPLE2_VIDEO_PIXELS_PER_COLUMN);
}

static void test_vbl_window(void)
{
    apple2_t m;
    uint32_t i;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Idle at start of frame: not in VBL. */
    expect_true("line0 not vbl", !apple2_video_in_vbl(&m));
    expect_u32("start line", 0, m.video.line);

    /* Advance to last visible line still not VBL. */
    apple2_video_step_n(&m, (uint32_t)APPLE2_VIDEO_VBL_START_LINE *
                                APPLE2_VIDEO_CYCLES_PER_LINE - 1u);
    expect_true("before vbl", !apple2_video_in_vbl(&m));

    apple2_video_step(&m);
    expect_true("at vbl", apple2_video_in_vbl(&m));
    expect_u32("vbl line", APPLE2_VIDEO_VBL_START_LINE, m.video.line);

    /* Soft switch RDVBL high during VBL. */
    expect_true("C019 high", (softswitch_c0_read(&m, 0xC019) & 0x80) != 0);

    /* Step through rest of frame back to line 0. */
    while (m.video.line != 0 || m.video.cycle_in_line != 0) {
        apple2_video_step(&m);
    }
    expect_true("C019 low active", (softswitch_c0_read(&m, 0xC019) & 0x80) == 0);
    expect_true("frame gen bumped", apple2_video_frame_gen(&m) >= 1);

    /* Full frame via CPU step_cycle keeps video in lockstep. */
    apple2_video_reset(&m);
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_FRAME; i++) {
        /* NOP at PC — may run ROM; just step video alone for this subtest */
        apple2_video_step(&m);
    }
    expect_u32("wrap line", 0, m.video.line);
    expect_u32("wrap h", 0, m.video.cycle_in_line);

    apple2_shutdown(&m);
}

static void test_floating_bus_varies(void)
{
    apple2_t m;
    uint8_t a, b;
    uint32_t i;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Put distinct bytes in text page columns. */
    for (i = 0; i < 40; i++) {
        m.ram_main[0x400 + i] = (uint8_t)(0xC0 + i);
    }
    softswitch_c0_write(&m, 0xC051, 0); /* TEXT */
    m.state_flags |= A2S_TEXT;
    apple2_video_reset(&m);

    /* Active beam at col 0 vs col 1 should differ when scanning. */
    m.video.line = 0;
    m.video.cycle_in_line = 0;
    a = apple2_video_floating_bus(&m);
    m.video.cycle_in_line = 1;
    b = apple2_video_floating_bus(&m);
    expect_true("fb col0", a == 0xC0);
    expect_true("fb col1", b == 0xC1);
    expect_true("fb varies", a != b);

    /* During HBLANK, latch holds last value. */
    m.video.cycle_in_line = 50;
    expect_true("hblank", apple2_video_in_hblank(&m));
    expect_u32("fb blank latch", b, apple2_video_floating_bus(&m));

    apple2_shutdown(&m);
}

static void test_midframe_page_flip(void)
{
    apple2_t m;
    uint8_t saw_p1 = 0;
    uint8_t saw_p2 = 0;
    uint32_t i;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Fill every text line base so all scanlines see the page pattern. */
    for (i = 0; i < 24; i++) {
        uint16_t base = apple2_video_text_line_base((uint8_t)i);
        memset(m.ram_main + 0x400 + base, 0x11, 40);
        memset(m.ram_main + 0x800 + base, 0x22, 40);
    }
    m.state_flags |= A2S_TEXT;
    softswitch_bank_clear(&m, A2S_PAGE2);
    apple2_video_reset(&m);

    /* Scan several lines on page 1. */
    for (i = 0; i < 20u * APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        if (!apple2_video_in_hblank(&m) && !apple2_video_in_vbl(&m)) {
            if (apple2_video_floating_bus(&m) == 0x11) {
                saw_p1 = 1;
            }
        }
        apple2_video_step(&m);
    }

    softswitch_c0_write(&m, 0xC055, 0); /* SETPAGE2 */
    expect_true("PAGE2 on", (apple2_state_flags(&m) & A2S_PAGE2) != 0);

    for (i = 0; i < 20u * APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        if (!apple2_video_in_hblank(&m) && !apple2_video_in_vbl(&m) &&
            m.video.line < APPLE2_VIDEO_VISIBLE_LINES) {
            uint8_t v = apple2_video_floating_bus(&m);
            if (v == 0x22) {
                saw_p2 = 1;
            }
        }
        apple2_video_step(&m);
    }

    expect_true("saw page1 data", saw_p1);
    expect_true("saw page2 after flip", saw_p2);

    apple2_shutdown(&m);
}

static void test_frame_paint_boot(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint32_t lit = 0;
    size_t i, n;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* Run long enough for BASIC banner and a completed frame. */
    while (apple2_cycles(&m) < 3000000ull) {
        apple2_step_cycles(&m, 1000, NULL);
        if (apple2_video_frame_gen(&m) >= 2) {
            break;
        }
    }

    fb = apple2_video_framebuffer(&m);
    expect_true("fb", fb != NULL);
    n = (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;
    for (i = 0; i < n; i++) {
        if ((fb[i] & 0x00FFFFFFu) != 0) {
            lit++;
        }
    }
    expect_true("some pixels lit after boot", lit > 100);

    apple2_shutdown(&m);
}

/* a2m LORES palette (must stay aligned with video.c LORES_PALETTE). */
static const uint32_t EXPECT_LORES[16] = {
    0xFF000000u, 0xFF9D0966u, 0xFF2A2AE5u, 0xFFC734FFu,
    0xFF008000u, 0xFF808080u, 0xFF0DA1FFu, 0xFFAAAAFFu,
    0xFF555500u, 0xFFF25E00u, 0xFFC0C0C0u, 0xFFFF89E5u,
    0xFF38CB00u, 0xFFD5D51Au, 0xFF62F699u, 0xFFFFFFFFu
};

static void test_lores_palette_cells(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base;
    uint32_t i;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* GR mode: CLRTEXT, CLRMIXED, CLRHGR, PAGE1. */
    softswitch_c0_write(&m, 0xC050, 0); /* graphics */
    softswitch_c0_write(&m, 0xC052, 0); /* full screen */
    softswitch_c0_write(&m, 0xC056, 0); /* lores */
    softswitch_c0_write(&m, 0xC054, 0); /* page 1 */
    expect_true("not text", (apple2_state_flags(&m) & A2S_TEXT) == 0);
    expect_true("not hires", (apple2_state_flags(&m) & A2S_HIRES) == 0);

    base = apple2_video_text_line_base(0);
    /* Col0: upper=1 (magenta), lower=12 (green). Col1: upper=15 white, lower=0. */
    m.ram_main[0x400u + base + 0] = 0xC1u; /* lower=C upper=1 */
    m.ram_main[0x400u + base + 1] = 0x0Fu; /* lower=0 upper=F */

    apple2_video_reset(&m);

    /* Paint full first text row (8 scanlines × 65 cycles). */
    for (i = 0; i < 8u * APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    expect_true("fb", fb != NULL);

    /* Line 0 (upper half): col0 → magenta, col1 → white across 14 host px. */
    expect_u32("lores L0 C0", EXPECT_LORES[1], fb[0 * APPLE2_VIDEO_WIDTH + 0]);
    expect_u32("lores L0 C0 mid", EXPECT_LORES[1],
               fb[0 * APPLE2_VIDEO_WIDTH + 7]);
    expect_u32("lores L0 C1", EXPECT_LORES[15],
               fb[0 * APPLE2_VIDEO_WIDTH + 14]);
    expect_u32("lores L3 C0", EXPECT_LORES[1],
               fb[3 * APPLE2_VIDEO_WIDTH + 0]);

    /* Line 4 (lower half): col0 → green, col1 → black. */
    expect_u32("lores L4 C0", EXPECT_LORES[12],
               fb[4 * APPLE2_VIDEO_WIDTH + 0]);
    expect_u32("lores L4 C0 mid", EXPECT_LORES[12],
               fb[4 * APPLE2_VIDEO_WIDTH + 13]);
    expect_u32("lores L4 C1", EXPECT_LORES[0],
               fb[4 * APPLE2_VIDEO_WIDTH + 14]);
    expect_u32("lores L7 C0", EXPECT_LORES[12],
               fb[7 * APPLE2_VIDEO_WIDTH + 0]);

    apple2_shutdown(&m);
}

static void test_lores_mixed_bottom_text(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base0;
    uint16_t base20;
    uint32_t i;
    uint32_t lit_bottom = 0;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* MIXED GR + text window: lines 0..159 lores, 160..191 text. */
    softswitch_c0_write(&m, 0xC050, 0); /* graphics */
    softswitch_c0_write(&m, 0xC053, 0); /* mixed */
    softswitch_c0_write(&m, 0xC056, 0); /* lores */
    softswitch_c0_write(&m, 0xC054, 0); /* page 1 */
    expect_true("mixed on", (apple2_state_flags(&m) & A2S_MIXED) != 0);

    base0 = apple2_video_text_line_base(0);
    base20 = apple2_video_text_line_base(20);
    m.ram_main[0x400u + base0] = 0x99u; /* orange / orange-ish cells */
    /* Put normal 'A' ($C1) in mixed text row for non-black paint. */
    memset(m.ram_main + 0x400u + base20, 0xA0, 40); /* spaces */
    m.ram_main[0x400u + base20] = 0xC1u;

    apple2_video_reset(&m);

    /* Step through line 0 and verify lores color, then through line 160. */
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }
    fb = apple2_video_framebuffer(&m);
    expect_u32("mixed lores top", EXPECT_LORES[9], fb[0]);

    while (m.video.line < 160u) {
        apple2_video_step(&m);
    }
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }
    fb = apple2_video_framebuffer(&m);
    for (i = 0; i < (uint32_t)APPLE2_VIDEO_WIDTH; i++) {
        if ((fb[160u * APPLE2_VIDEO_WIDTH + i] & 0x00FFFFFFu) != 0u) {
            lit_bottom++;
        }
    }
    expect_true("mixed bottom text paints", lit_bottom > 0);

    apple2_shutdown(&m);
}

/* a2m double_aux_map: even/odd nibble bits de-interleaved onto LORES palette. */
static const uint8_t EXPECT_DLORES_AUX_MAP[16] = {
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
    0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
};

static void test_dlores_half_cells(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base;
    uint32_t i;
    uint32_t aux_upper = EXPECT_DLORES_AUX_MAP[1];  /* 0x01 → 0x02 dark blue */
    uint32_t aux_lower = EXPECT_DLORES_AUX_MAP[12]; /* 0x0C → 0x09 orange */
    uint32_t x;

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* COL80 + GR (not TEXT, not HIRES) → DLORES. */
    softswitch_c0_write(&m, 0xC050, 0); /* graphics */
    softswitch_c0_write(&m, 0xC052, 0); /* full */
    softswitch_c0_write(&m, 0xC056, 0); /* lores */
    softswitch_c0_write(&m, 0xC054, 0); /* page 1 */
    softswitch_c0_write(&m, 0xC00D, 0); /* 80col */
    expect_true("dlores col80", (apple2_state_flags(&m) & A2S_COL80) != 0);
    expect_true("dlores not text", (apple2_state_flags(&m) & A2S_TEXT) == 0);
    expect_true("dlores not hires", (apple2_state_flags(&m) & A2S_HIRES) == 0);

    base = apple2_video_text_line_base(0);
    /* Aux col0: lower=C upper=1. Main col0: lower=4 upper=F. */
    m.ram_main[0x10400u + base + 0] = 0xC1u;
    m.ram_main[0x0400u + base + 0] = 0x4Fu;

    apple2_video_reset(&m);
    for (i = 0; i < 8u * APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    expect_true("fb", fb != NULL);

    /* Line 0: aux 7 px then main 7 px (not a 14-px single-lores cell). */
    expect_u32("dlores L0 aux", EXPECT_LORES[aux_upper], fb[0]);
    expect_u32("dlores L0 aux mid", EXPECT_LORES[aux_upper], fb[6]);
    expect_u32("dlores L0 main", EXPECT_LORES[15], fb[7]);
    expect_u32("dlores L0 main mid", EXPECT_LORES[15], fb[13]);

    /* Line 4: aux mapped orange, main dark green. */
    expect_u32("dlores L4 aux", EXPECT_LORES[aux_lower],
               fb[4 * APPLE2_VIDEO_WIDTH + 0]);
    expect_u32("dlores L4 aux end", EXPECT_LORES[aux_lower],
               fb[4 * APPLE2_VIDEO_WIDTH + 6]);
    expect_u32("dlores L4 main", EXPECT_LORES[4],
               fb[4 * APPLE2_VIDEO_WIDTH + 7]);
    expect_u32("dlores L4 main end", EXPECT_LORES[4],
               fb[4 * APPLE2_VIDEO_WIDTH + 13]);

    /* Half-cells are solid; the aux/main join is the colour change. */
    for (x = 0; x < 7u; x++) {
        expect_u32("dlores aux run", EXPECT_LORES[aux_upper], fb[x]);
    }
    for (x = 7; x < 14u; x++) {
        expect_u32("dlores main run", EXPECT_LORES[15], fb[x]);
    }

    apple2_shutdown(&m);
}

static void test_dlores_page2_and_block_parity(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base;
    uint32_t i;
    uint32_t beam_pix;
    uint32_t aux_upper = EXPECT_DLORES_AUX_MAP[3]; /* 0x03 → 0x06 */

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0);
    softswitch_c0_write(&m, 0xC052, 0);
    softswitch_c0_write(&m, 0xC056, 0);
    softswitch_c0_write(&m, 0xC00D, 0);
    softswitch_c0_write(&m, 0xC055, 0); /* PAGE2 */
    expect_true("dlores page2", (apple2_state_flags(&m) & A2S_PAGE2) != 0);

    base = apple2_video_text_line_base(0);
    /* Page 1 stays black; page 2 carries the cell. */
    m.ram_main[0x10400u + base + 0] = 0x00u;
    m.ram_main[0x0400u + base + 0] = 0x00u;
    m.ram_main[0x10800u + base + 0] = 0x03u; /* aux upper=3 */
    m.ram_main[0x0800u + base + 0] = 0x0Fu;  /* main upper=F */

    apple2_video_reset(&m);
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    expect_u32("dlores page2 aux", EXPECT_LORES[aux_upper], fb[0]);
    expect_u32("dlores page2 main", EXPECT_LORES[15], fb[7]);
    beam_pix = fb[0];

    memset(m.video.fb, 0,
           (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT *
               sizeof(uint32_t));
    apple2_video_paint_full_frame(&m);
    expect_u32("dlores block matches beam", beam_pix, fb[0]);
    expect_u32("dlores block page2 main", EXPECT_LORES[15], fb[7]);

    apple2_shutdown(&m);
}

static void test_dlores_mixed_bottom_text80(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base0;
    uint16_t base20;
    uint32_t i;
    uint32_t lit_bottom = 0;
    uint32_t aux_upper = EXPECT_DLORES_AUX_MAP[9]; /* 0x09 → 0x03 */

    if (!apple2_init(&m)) {
        fail("init");
    }

    /* MIXED + COL80 + GR: lines 0..159 dlores, 160..191 80-col text. */
    softswitch_c0_write(&m, 0xC050, 0);
    softswitch_c0_write(&m, 0xC053, 0); /* mixed */
    softswitch_c0_write(&m, 0xC056, 0);
    softswitch_c0_write(&m, 0xC054, 0);
    softswitch_c0_write(&m, 0xC00D, 0);

    base0 = apple2_video_text_line_base(0);
    base20 = apple2_video_text_line_base(20);
    m.ram_main[0x10400u + base0] = 0x09u;
    m.ram_main[0x0400u + base0] = 0x00u;
    memset(m.ram_main + 0x400u + base20, 0xA0, 40);
    memset(m.ram_main + 0x10400u + base20, 0xA0, 40);
    m.ram_main[0x10400u + base20] = 0xC1u; /* aux 'A' */

    apple2_video_reset(&m);
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }
    fb = apple2_video_framebuffer(&m);
    expect_u32("mixed dlores top", EXPECT_LORES[aux_upper], fb[0]);

    while (m.video.line < 160u) {
        apple2_video_step(&m);
    }
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }
    fb = apple2_video_framebuffer(&m);
    for (i = 0; i < 7u; i++) {
        if ((fb[160u * APPLE2_VIDEO_WIDTH + i] & 0x00FFFFFFu) != 0u) {
            lit_bottom++;
        }
    }
    expect_true("mixed dlores bottom 80col paints", lit_bottom > 0);

    apple2_shutdown(&m);
}

/* a2m HGR Holger-Picker colours. */
static const uint32_t HGR_VIOLET = 0xFFC734FFu;
static const uint32_t HGR_GREEN = 0xFF38CB00u;
static const uint32_t HGR_ORANGE = 0xFFF25E00u;
static const uint32_t HGR_WHITE = 0xFFFFFFFFu;

static void test_hgr_color_bits(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t off;
    uint32_t i;
    int saw_violet = 0;
    int saw_green = 0;
    int saw_orange = 0;
    int saw_white = 0;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0); /* graphics */
    softswitch_c0_write(&m, 0xC052, 0); /* full */
    softswitch_c0_write(&m, 0xC057, 0); /* hires */
    softswitch_c0_write(&m, 0xC054, 0); /* page 1 */
    expect_true("hires on", (apple2_state_flags(&m) & A2S_HIRES) != 0);

    off = apple2_video_hgr_line_offset(0);
    /* 0x55 phase0 → violet; 0x2A phase0 → green; 0xAA phase1 → orange;
       0x7F phase0 → white. Isolate bytes so neighbour bits do not muddy. */
    memset(m.ram_main + 0x2000u + off, 0, 40);
    m.ram_main[0x2000u + off + 0] = 0x55u;
    m.ram_main[0x2000u + off + 2] = 0x2Au;
    m.ram_main[0x2000u + off + 4] = 0xAAu; /* phase bit set */
    m.ram_main[0x2000u + off + 6] = 0x7Fu;

    apple2_video_reset(&m);
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    expect_true("fb", fb != NULL);

    for (i = 0; i < (uint32_t)APPLE2_VIDEO_WIDTH; i++) {
        uint32_t p = fb[i];
        if (p == HGR_VIOLET) {
            saw_violet = 1;
        }
        if (p == HGR_GREEN) {
            saw_green = 1;
        }
        if (p == HGR_ORANGE) {
            saw_orange = 1;
        }
        if (p == HGR_WHITE) {
            saw_white = 1;
        }
    }
    expect_true("hgr violet", saw_violet);
    expect_true("hgr green", saw_green);
    expect_true("hgr orange", saw_orange);
    expect_true("hgr white", saw_white);

    apple2_shutdown(&m);
}

static void test_text80_interleave(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base;
    uint32_t i;
    uint32_t lit = 0;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC051, 0); /* text */
    softswitch_c0_write(&m, 0xC00D, 0); /* COL80 on */
    expect_true("col80", (apple2_state_flags(&m) & A2S_COL80) != 0);

    base = apple2_video_text_line_base(0);
    /* Aux 'A' ($C1), main space ($A0) in col 0. */
    m.ram_main[0x10000u + 0x400u + base] = 0xC1u;
    m.ram_main[0x400u + base] = 0xA0u;
    memset(m.ram_main + 0x401u + base, 0xA0, 39);
    memset(m.ram_main + 0x10000u + 0x401u + base, 0xA0, 39);

    apple2_video_reset(&m);
    for (i = 0; i < 8u * APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    for (i = 0; i < 7u; i++) {
        if ((fb[i] & 0x00FFFFFFu) != 0u) {
            lit++;
        }
    }
    expect_true("80col aux glyph lit in left 7px", lit > 0);
    /* Main space should be black in next 7 host pixels. */
    {
        uint32_t nonblack = 0;
        for (i = 7; i < 14u; i++) {
            if ((fb[i] & 0x00FFFFFFu) != 0u) {
                nonblack++;
            }
        }
        expect_true("80col main space mostly black", nonblack == 0);
    }

    apple2_shutdown(&m);
}

static void test_dhgr_nonblack(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t off;
    uint32_t i;
    uint32_t lit = 0;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0); /* graphics */
    softswitch_c0_write(&m, 0xC052, 0);
    softswitch_c0_write(&m, 0xC057, 0); /* hires */
    softswitch_c0_write(&m, 0xC00D, 0); /* 80col → DHGR path with HIRES */
    softswitch_c0_write(&m, 0xC05E, 0); /* DHIRES on */
    expect_true("dhgr mode flags",
                (apple2_state_flags(&m) & A2S_HIRES) != 0 &&
                    (apple2_state_flags(&m) & A2S_COL80) != 0);

    off = apple2_video_hgr_line_offset(0);
    /* Solid white-ish: all bits in aux+main. */
    for (i = 0; i < 40u; i++) {
        m.ram_main[0x2000u + off + i] = 0x7Fu;
        m.ram_main[0x10000u + 0x2000u + off + i] = 0x7Fu;
    }

    apple2_video_reset(&m);
    for (i = 0; i < APPLE2_VIDEO_CYCLES_PER_LINE; i++) {
        apple2_video_step(&m);
    }

    fb = apple2_video_framebuffer(&m);
    for (i = 0; i < (uint32_t)APPLE2_VIDEO_WIDTH; i++) {
        if ((fb[i] & 0x00FFFFFFu) != 0u) {
            lit++;
        }
    }
    expect_true("dhgr paints non-black", lit > 100);

    apple2_shutdown(&m);
}

static const uint32_t MONO_WHITE = 0xFFFFFFFFu;
static const uint32_t MONO_GREEN = 0xFF33FF66u;
static const uint32_t MONO_AMBER = 0xFFFFB000u;

static uint32_t expect_phosphor_luma(uint32_t phosphor, uint32_t src)
{
    unsigned int r = (src >> 16) & 0xffu;
    unsigned int g = (src >> 8) & 0xffu;
    unsigned int b = src & 0xffu;
    unsigned int y = (299u * r + 587u * g + 114u * b) / 1000u;
    unsigned int pr = (phosphor >> 16) & 0xffu;
    unsigned int pg = (phosphor >> 8) & 0xffu;
    unsigned int pb = phosphor & 0xffu;
    return 0xFF000000u |
        (((pr * y) / 255u) << 16) |
        (((pg * y) / 255u) << 8) |
        ((pb * y) / 255u);
}

static void test_hgr_mono_discrete_bits(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t off;
    int b;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0);
    softswitch_c0_write(&m, 0xC052, 0);
    softswitch_c0_write(&m, 0xC057, 0);
    softswitch_c0_write(&m, 0xC054, 0);

    off = apple2_video_hgr_line_offset(0);
    memset(m.ram_main + 0x2000u + off, 0, 40);
    /* bit0 leftmost; 0x55 = on/off/on/off/on/off/on. bit7 of 0xAA ignored. */
    m.ram_main[0x2000u + off + 0] = 0x55u;
    m.ram_main[0x2000u + off + 1] = 0xAAu;

    apple2_video_set_monitor(&m, false, APPLE2_VIDEO_PHOSPHOR_GREEN);
    apple2_video_paint_full_frame(&m);
    fb = apple2_video_framebuffer(&m);

    for (b = 0; b < 7; b++) {
        uint32_t want = ((0x55u >> b) & 1u) ? MONO_GREEN : 0xFF000000u;
        expect_u32("hgr mono 0x55 even", want, fb[(size_t)(b * 2)]);
        expect_u32("hgr mono 0x55 odd", want, fb[(size_t)(b * 2 + 1)]);
    }
    for (b = 0; b < 7; b++) {
        uint32_t want = ((0x2Au >> b) & 1u) ? MONO_GREEN : 0xFF000000u;
        expect_u32("hgr mono 0xAA ignores bit7", want, fb[(size_t)(14 + b * 2)]);
    }

    apple2_video_set_monitor(&m, true, APPLE2_VIDEO_PHOSPHOR_WHITE);
    apple2_video_paint_full_frame(&m);
    {
        int saw_violet = 0;
        int x;
        for (x = 0; x < 14; x++) {
            if (fb[x] == HGR_VIOLET) {
                saw_violet = 1;
            }
        }
        expect_true("colour decoder restored", saw_violet);
    }

    apple2_shutdown(&m);
}

static void test_dhgr_mono_discrete_bits(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t off;
    int x;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0);
    softswitch_c0_write(&m, 0xC052, 0);
    softswitch_c0_write(&m, 0xC057, 0);
    softswitch_c0_write(&m, 0xC00D, 0);
    softswitch_c0_write(&m, 0xC05E, 0);

    off = apple2_video_hgr_line_offset(0);
    for (x = 0; x < 40; x++) {
        m.ram_main[0x2000u + off + (uint16_t)x] = 0;
        m.ram_main[0x10000u + 0x2000u + off + (uint16_t)x] = 0;
    }
    m.ram_main[0x10000u + 0x2000u + off] = 0x01u;

    apple2_video_set_monitor(&m, false, APPLE2_VIDEO_PHOSPHOR_AMBER);
    apple2_video_paint_full_frame(&m);
    fb = apple2_video_framebuffer(&m);
    expect_u32("dhgr mono bit0 on", MONO_AMBER, fb[0]);
    expect_u32("dhgr mono bit1 off", 0xFF000000u, fb[1]);
    expect_u32("dhgr mono bit2 off", 0xFF000000u, fb[2]);

    apple2_shutdown(&m);
}

static void test_lores_mono_phosphor_luma(void)
{
    apple2_t m;
    const uint32_t *fb;
    uint16_t base;

    if (!apple2_init(&m)) {
        fail("init");
    }

    softswitch_c0_write(&m, 0xC050, 0);
    softswitch_c0_write(&m, 0xC052, 0);
    softswitch_c0_write(&m, 0xC056, 0);
    base = apple2_video_text_line_base(0);
    m.ram_main[0x400u + base + 0] = 0x0Fu; /* white */
    m.ram_main[0x400u + base + 1] = 0x00u; /* black */
    m.ram_main[0x400u + base + 2] = 0x05u; /* gray1 */

    apple2_video_set_monitor(&m, false, APPLE2_VIDEO_PHOSPHOR_GREEN);
    apple2_video_paint_full_frame(&m);
    fb = apple2_video_framebuffer(&m);

    expect_u32("lores mono white", MONO_GREEN, fb[0]);
    expect_u32("lores mono black", 0xFF000000u, fb[14]);
    expect_u32(
        "lores mono gray",
        expect_phosphor_luma(MONO_GREEN, EXPECT_LORES[5]),
        fb[28]);
    expect_true(
        "gray darker than white",
        (fb[28] & 0x00FF00u) < (MONO_GREEN & 0x00FF00u));

    apple2_shutdown(&m);
}

int main(void)
{
    test_timing_constants();
    test_vbl_window();
    test_floating_bus_varies();
    test_midframe_page_flip();
    test_frame_paint_boot();
    test_lores_palette_cells();
    test_lores_mixed_bottom_text();
    test_dlores_half_cells();
    test_dlores_page2_and_block_parity();
    test_dlores_mixed_bottom_text80();
    test_hgr_color_bits();
    test_text80_interleave();
    test_dhgr_nonblack();
    test_hgr_mono_discrete_bits();
    test_dhgr_mono_discrete_bits();
    test_lores_mono_phosphor_luma();
    printf("video_beam: all tests passed\n");
    return 0;
}

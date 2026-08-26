#include "apple2.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static int count_nonblack(const uint32_t *fb, size_t n)
{
    size_t i;
    int c = 0;
    for (i = 0; i < n; i++) {
        if ((fb[i] & 0x00FFFFFFu) != 0u) {
            c++;
        }
    }
    return c;
}

int main(void)
{
    apple2_t m;
    const uint32_t *fb;
    size_t pixels =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;
    int nonblack;
    uint16_t line_off;
    int col;

    expect_true("init", apple2_init(&m));
    fb = apple2_video_framebuffer(&m);
    expect_true("fb", fb != NULL);

    /* --- TEXT 40: fill page with inverse spaces (visible) --- */
    m.state_flags = A2S_TEXT;
    {
        uint16_t row;
        for (row = 0; row < 24u; row++) {
            uint16_t base = apple2_video_text_line_base((uint8_t)row);
            for (col = 0; col < 40; col++) {
                m.ram_main[0x0400u + base + (uint16_t)col] = 0xA0u; /* normal space */
            }
        }
        /* Put 'A' (0xC1) on row 0 col 0 — solid glyph bits for non-black. */
        m.ram_main[0x0400u + apple2_video_text_line_base(0) + 0] = 0xC1u;
    }
    apple2_video_paint_full_frame(&m);
    nonblack = count_nonblack(fb, pixels);
    expect_true("text40 nonblack", nonblack > 0);

    /* Display override paints another page without changing real switches. */
    {
        uint32_t actual_flags = m.state_flags;
        uint8_t actual_bus;
        uint16_t row;
        for (row = 0; row < 24u; row++) {
            uint16_t base = apple2_video_text_line_base((uint8_t)row);
            for (col = 0; col < 40; col++) {
                m.ram_main[0x0800u + base + (uint16_t)col] = 0xA0u;
            }
        }
        m.video.line = 0;
        m.video.cycle_in_line = 0;
        m.ram_main[0x0400u] = 0xC1u;
        m.ram_main[0x0800u] = 0xA0u;
        apple2_video_set_display_override(&m, true, A2S_TEXT | A2S_PAGE2);
        apple2_video_paint_full_frame(&m);
        expect_true("override page2 blank", count_nonblack(fb, pixels) == 0);
        expect_true("override preserves actual flags", m.state_flags == actual_flags);
        actual_bus = apple2_video_floating_bus(&m);
        expect_true("override preserves actual scanner", actual_bus == 0xC1u);
        apple2_video_set_display_override(&m, false, 0u);
        apple2_video_paint_full_frame(&m);
        expect_true("override off restores actual page", count_nonblack(fb, pixels) > 0);
    }

    /* --- HGR: white line pattern --- */
    memset(m.ram_main + 0x2000, 0, 0x2000);
    m.state_flags = A2S_HIRES;
    line_off = apple2_video_hgr_line_offset(0);
    for (col = 0; col < 40; col++) {
        m.ram_main[0x2000u + line_off + (uint16_t)col] = 0x7Fu; /* all dots on */
    }
    apple2_video_paint_full_frame(&m);
    nonblack = count_nonblack(fb, pixels);
    expect_true("hgr nonblack", nonblack > 100);

    /* --- LORES: solid colour cell --- */
    memset(m.ram_main + 0x0400, 0, 0x400);
    m.state_flags = 0; /* GR */
    m.ram_main[0x0400u + apple2_video_text_line_base(0) + 0] = 0xFFu; /* white cell */
    apple2_video_paint_full_frame(&m);
    nonblack = count_nonblack(fb, pixels);
    expect_true("lores nonblack", nonblack > 0);
    /* Top of cell (line 0) should be white (nibble 0xF). */
    expect_true(
        "lores white pixel",
        (fb[0] & 0x00FFFFFFu) == 0x00FFFFFFu);

    /* --- DLORES: COL80 GR, aux then main 7-px half-columns --- */
    memset(m.ram_main + 0x0400, 0, 0x400);
    memset(m.ram_main + 0x10400, 0, 0x400);
    m.state_flags = A2S_COL80;
    /* Aux 0x01 → double_aux_map 0x02 (dark blue); main 0x0F white. */
    m.ram_main[0x10400u + apple2_video_text_line_base(0) + 0] = 0x01u;
    m.ram_main[0x0400u + apple2_video_text_line_base(0) + 0] = 0x0Fu;
    apple2_video_paint_full_frame(&m);
    nonblack = count_nonblack(fb, pixels);
    expect_true("dlores nonblack", nonblack > 0);
    expect_true("dlores aux mapped", fb[0] == 0xFF2A2AE5u);
    expect_true("dlores aux 7px", fb[6] == 0xFF2A2AE5u);
    expect_true("dlores main white", fb[7] == 0xFFFFFFFFu);

    /* Beam position unchanged by block paint. */
    m.video.line = 50;
    m.video.cycle_in_line = 12;
    apple2_video_paint_full_frame(&m);
    expect_true("beam line preserved", m.video.line == 50);
    expect_true("beam h preserved", m.video.cycle_in_line == 12);

    /* Explicit-destination presentation paint leaves canonical pixels and
       every timing/latch field untouched. */
    {
        uint32_t *scratch = malloc(pixels * sizeof(*scratch));
        uint32_t *canonical_copy = malloc(pixels * sizeof(*canonical_copy));
        uint64_t frame_number = m.video.frame_number;
        uint32_t frame_gen = m.video.frame_gen;
        uint8_t last_video_byte = m.video.last_video_byte;
        bool frame_ready = m.video.frame_ready;
        bool paint_enabled = m.video.paint_enabled;

        expect_true("scratch allocations", scratch != NULL && canonical_copy != NULL);
        memcpy(canonical_copy, fb, pixels * sizeof(*canonical_copy));
        memset(scratch, 0x5A, pixels * sizeof(*scratch));
        expect_true(
            "paint explicit destination",
            apple2_video_paint_full_frame_to(&m, scratch, pixels));
        expect_true("scratch got picture", count_nonblack(scratch, pixels) > 0);
        expect_true(
            "canonical pixels preserved",
            memcmp(canonical_copy, fb, pixels * sizeof(*canonical_copy)) == 0);
        expect_true("scratch beam line", m.video.line == 50);
        expect_true("scratch beam h", m.video.cycle_in_line == 12);
        expect_true("scratch frame number", m.video.frame_number == frame_number);
        expect_true("scratch frame gen", m.video.frame_gen == frame_gen);
        expect_true("scratch latch", m.video.last_video_byte == last_video_byte);
        expect_true("scratch frame ready", m.video.frame_ready == frame_ready);
        expect_true("scratch paint enabled", m.video.paint_enabled == paint_enabled);
        expect_true(
            "short destination rejected",
            !apple2_video_paint_full_frame_to(&m, scratch, pixels - 1u));
        free(canonical_copy);
        free(scratch);
    }

    /* Reseed from cycles: known position. */
    m.cpu.cpu.cycles = (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE * 10u + 7u;
    apple2_video_reseed_from_cycles(&m);
    expect_true("reseed line", m.video.line == 10);
    expect_true("reseed h", m.video.cycle_in_line == 7);

    /* A-lite jump: wrap a frame and land in VBL without painting. */
    {
        uint32_t pixel0;
        m.video.line = 261;
        m.video.cycle_in_line = 60;
        m.video.frame_number = 5;
        m.video.paint_enabled = false;
        pixel0 = fb[0];
        apple2_video_advance_alite(&m, 10u);
        expect_true("alite wrap line", m.video.line == 0);
        expect_true("alite wrap h", m.video.cycle_in_line == 5);
        expect_true("alite wrap frame", m.video.frame_number == 6);
        expect_true("alite wrap not vbl", !apple2_video_in_vbl(&m));
        apple2_video_advance_alite(
            &m,
            (uint32_t)APPLE2_VIDEO_VBL_START_LINE * APPLE2_VIDEO_CYCLES_PER_LINE);
        expect_true("alite vbl line", m.video.line == APPLE2_VIDEO_VBL_START_LINE);
        expect_true("alite vbl", apple2_video_in_vbl(&m));
        expect_true(
            "alite C019", (softswitch_c0_read(&m, 0xC019) & 0x80u) != 0);
        expect_true("alite does not paint", fb[0] == pixel0);
        m.video.paint_enabled = true;
    }

    /* Max instruction path advances A-lite H/V (and thus $C019) by ran. */
    {
        uint16_t line0;
        uint16_t h0;
        size_t ran;
        uint32_t pos1;
        m.video.line = 10;
        m.video.cycle_in_line = 7;
        m.video.paint_enabled = false;
        line0 = m.video.line;
        h0 = m.video.cycle_in_line;
        ran = apple2_step_instruction_max(&m);
        expect_true("max insn ran", ran > 0);
        pos1 = ((uint32_t)line0 * (uint32_t)APPLE2_VIDEO_CYCLES_PER_LINE +
                (uint32_t)h0 + (uint32_t)ran) %
            (uint32_t)APPLE2_VIDEO_CYCLES_PER_FRAME;
        expect_true(
            "max insn beam advanced",
            m.video.line ==
                    (uint16_t)(pos1 / (uint32_t)APPLE2_VIDEO_CYCLES_PER_LINE) &&
                m.video.cycle_in_line ==
                    (uint16_t)(pos1 % (uint32_t)APPLE2_VIDEO_CYCLES_PER_LINE));
    }

    /*
     * Guest $C019 waiters (Total Replay, most games) hang forever if max
     * freezes the beam. Drive enough max instructions to enter and leave VBL.
     */
    {
        uint64_t start;
        uint32_t guard;
        const uint64_t vbl_cycles =
            (uint64_t)APPLE2_VIDEO_VBL_START_LINE *
            (uint64_t)APPLE2_VIDEO_CYCLES_PER_LINE;
        const uint64_t frame_cycles = (uint64_t)APPLE2_VIDEO_CYCLES_PER_FRAME;

        m.video.paint_enabled = false;
        m.video.line = 0;
        m.video.cycle_in_line = 0;
        expect_true("max start not vbl", !apple2_video_in_vbl(&m));
        expect_true(
            "max start C019 low",
            (softswitch_c0_read(&m, 0xC019) & 0x80u) == 0);

        start = apple2_cycles(&m);
        guard = 0u;
        while ((apple2_cycles(&m) - start) < (vbl_cycles + 8u)) {
            expect_true("max to vbl ran", apple2_step_instruction_max(&m) > 0);
            if (++guard > 2000000u) {
                expect_true("max to vbl finished", 0);
                break;
            }
        }
        expect_true("max path reaches vbl", apple2_video_in_vbl(&m));
        expect_true(
            "max path C019 high",
            (softswitch_c0_read(&m, 0xC019) & 0x80u) != 0);

        start = apple2_cycles(&m);
        guard = 0u;
        while ((apple2_cycles(&m) - start) <
               ((frame_cycles - vbl_cycles) + 8u)) {
            expect_true("max out of vbl ran", apple2_step_instruction_max(&m) > 0);
            if (++guard > 2000000u) {
                expect_true("max out of vbl finished", 0);
                break;
            }
        }
        expect_true("max path leaves vbl", !apple2_video_in_vbl(&m));
        expect_true(
            "max path C019 low",
            (softswitch_c0_read(&m, 0xC019) & 0x80u) == 0);
        m.video.paint_enabled = true;
    }

    apple2_shutdown(&m);
    printf("OK video_block_paint\n");
    return 0;
}

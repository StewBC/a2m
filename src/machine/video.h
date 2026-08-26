#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct apple2;

/*
 * NTSC Apple II video timing (Φ0 domain).
 *
 * 65 cycles/line × 262 lines = 17030 cycles/frame.
 * Visible: lines 0..191 (text/HGR). VBL soft switch high on lines 192..261.
 * Horizontal: cycles 0..39 video scanner / display, 40..64 HBLANK (model).
 */
enum {
    APPLE2_VIDEO_CYCLES_PER_LINE = 65,
    APPLE2_VIDEO_LINES_PER_FRAME = 262,
    APPLE2_VIDEO_CYCLES_PER_FRAME =
        APPLE2_VIDEO_CYCLES_PER_LINE * APPLE2_VIDEO_LINES_PER_FRAME, /* 17030 */
    APPLE2_VIDEO_VISIBLE_LINES = 192,
    APPLE2_VIDEO_VBL_START_LINE = 192,
    APPLE2_VIDEO_H_VISIBLE_CYCLES = 40,
    /* Always 560-wide ARGB: 40 scanner columns × 14 host pixels (7 logical
       dots × 2). 40-col/HGR/LORES pixel-double; 80-col/DLORES/DHGR use full
       width (DLORES = two 7-px half-columns per scanner column). */
    APPLE2_VIDEO_WIDTH = 560,
    APPLE2_VIDEO_HEIGHT = 192,
    APPLE2_VIDEO_PIXELS_PER_COLUMN = 14
};

typedef enum apple2_video_phosphor {
    APPLE2_VIDEO_PHOSPHOR_WHITE = 0,
    APPLE2_VIDEO_PHOSPHOR_GREEN = 1,
    APPLE2_VIDEO_PHOSPHOR_AMBER = 2
} apple2_video_phosphor;

typedef struct apple2_video {
    uint16_t cycle_in_line; /* 0..64 */
    uint16_t line;          /* 0..261 */
    uint64_t frame_number;
    uint32_t frame_gen;     /* bumps when a frame completes */

    uint8_t last_video_byte; /* floating-bus scanner latch */
    bool paint_enabled;
    bool display_override_enabled;
    uint32_t display_override_flags;
    /* Host monitor: 0 = colour artefact decoder, 1 = discrete bits. Not
       snapshotted; memset default is colour + white phosphor. */
    bool mono;
    apple2_video_phosphor phosphor;

    /* ARGB8888, row-major APPLE2_VIDEO_WIDTH × APPLE2_VIDEO_HEIGHT */
    uint32_t *fb;
    bool frame_ready; /* set when a frame just completed; cleared by consumer */
} apple2_video;

void apple2_video_init(struct apple2 *m);
void apple2_video_shutdown(struct apple2 *m);
void apple2_video_reset(struct apple2 *m);

/* Advance video by one Φ0 (must match each CPU cycle). */
void apple2_video_step(struct apple2 *m);

/* Advance video by N cycles (for multi-cycle CPU atomic fallback). */
void apple2_video_step_n(struct apple2 *m, uint32_t n);

/*
 * Max / A-lite: jump H/V (and thus $C019 VBL / HBL) by n Φ0 with no paint
 * and no scanner. O(1) so the max instruction loop stays flat-out.
 */
void apple2_video_advance_alite(struct apple2 *m, uint32_t n);

bool apple2_video_in_vbl(const struct apple2 *m);
bool apple2_video_in_hblank(const struct apple2 *m);

/* Scanner data for floating bus / RDVBL helpers. */
uint8_t apple2_video_floating_bus(struct apple2 *m);

const uint32_t *apple2_video_framebuffer(const struct apple2 *m);
uint32_t apple2_video_frame_gen(const struct apple2 *m);
bool apple2_video_take_frame_ready(struct apple2 *m);

/*
 * Full-frame (block) paint from RAM + softswitches into m->video.fb.
 * Does not advance the beam. Used for max turbo presentation paint.
 * Covers text40/80, lores, dlores, hgr, dhgr, mixed combos, page1/2.
 */
void apple2_video_paint_full_frame(struct apple2 *m);

/*
 * Paint a coherent full frame into caller-owned storage without changing the
 * canonical beam framebuffer or any video timing/latch state.  dst_pixels
 * must hold at least APPLE2_VIDEO_WIDTH * APPLE2_VIDEO_HEIGHT entries.
 */
bool apple2_video_paint_full_frame_to(
    struct apple2 *m,
    uint32_t *dst,
    size_t dst_pixels);

/*
 * Debugger presentation override. This changes only which display mode/page
 * the painter shows; hardware soft switches and floating-bus scans remain
 * driven by the machine's actual state_flags.
 */
void apple2_video_set_display_override(
    struct apple2 *m,
    bool enabled,
    uint32_t flags);

/* Host monitor decoder. colour=true is the artefact LUT path; false paints
   discrete on/off bits (HGR/DHGR/text) and 16 spaced phosphor fills for
   LORES/DLORES cells.
   Does not paint; caller issues paint_full_frame when a coherent frame is
   needed. */
void apple2_video_set_monitor(
    struct apple2 *m,
    bool colour,
    apple2_video_phosphor phosphor);

/*
 * After max free-run (video offline), re-seed beam H/V from total Φ0 so
 * finite/beam mode is coherent again. Does not paint.
 */
void apple2_video_reseed_from_cycles(struct apple2 *m);

/* Text-line base (0..23) within page $400 or $800. */
uint16_t apple2_video_text_line_base(uint8_t text_row);
/* HGR line base offset within $2000/$4000 page (0..191). */
uint16_t apple2_video_hgr_line_offset(uint8_t pixel_row);

/*
 * Soft-switch-locked pixel -> host RAM address (Machine Display probe).
 * flags: TEXT/MIXED/PAGE2/HIRES/COL80/DHIRES/80STORE (and ALTCHAR unused).
 * (px, py) are host framebuffer coords in 0..559 x 0..191.
 * host_addr uses the video plane convention: aux at +0x10000.
 */
typedef struct apple2_video_pixel_addr {
    uint16_t bank_base; /* page base, e.g. $0400 / $2000 / $4000 */
    uint16_t offset;    /* within page */
    uint32_t host_addr; /* CPU space; aux via +0x10000 */
    bool from_aux;
} apple2_video_pixel_addr;

bool apple2_video_pixel_address(
    uint32_t flags,
    uint16_t px,
    uint16_t py,
    apple2_video_pixel_addr *out);

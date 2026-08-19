#pragma once

/*
 * Product presentation frame for a2m (Apple ][+ / //e).
 *
 * Geometry is the active Apple II display only. Keep these macros in lockstep
 * with APPLE2_VIDEO_* in video.h (and the machine paint path). A mismatch is a
 * build error, not a silent F9 UV-crop bug.
 */

#include "video.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Preprocessor-visible so #if / static asserts can catch drift. */
#define DISPLAY_FRAME_WIDTH  560
#define DISPLAY_FRAME_HEIGHT 192

#if DISPLAY_FRAME_WIDTH != 560 || DISPLAY_FRAME_HEIGHT != 192
#error "DISPLAY_FRAME_* macros must describe the Apple II active display"
#endif

/* Cross-check against the machine video header (enum values). */
enum {
    DISPLAY_FRAME_WIDTH_CHECK = APPLE2_VIDEO_WIDTH,
    DISPLAY_FRAME_HEIGHT_CHECK = APPLE2_VIDEO_HEIGHT,
    DISPLAY_FRAME_PIXEL_FORMAT_ARGB8888 = 1
};

typedef char display_frame_width_matches_video_
    [(DISPLAY_FRAME_WIDTH == APPLE2_VIDEO_WIDTH) ? 1 : -1];
typedef char display_frame_height_matches_video_
    [(DISPLAY_FRAME_HEIGHT == APPLE2_VIDEO_HEIGHT) ? 1 : -1];

/* Metadata for a submitted host frame. Pixels live in a separate ARGB buffer
   (frontend_submit_argb_frame). */
typedef struct display_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes; /* pixels per row */
    uint32_t pixel_format;
    uint64_t frame_number;
    uint64_t machine_cycle; /* optional telemetry for frame-ring lookups */
} display_frame;

static inline bool display_frame_is_valid(const display_frame *frame)
{
    return frame != NULL
        && frame->width == (uint32_t)DISPLAY_FRAME_WIDTH
        && frame->height == (uint32_t)DISPLAY_FRAME_HEIGHT
        && frame->pixel_format == DISPLAY_FRAME_PIXEL_FORMAT_ARGB8888;
}

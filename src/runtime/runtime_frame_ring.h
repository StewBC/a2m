#pragma once

/* Rolling ARGB framebuffer ring — black box for late-pause screen recovery.
 *
 * Apple product frames are 560×192 ARGB8888. Entries carry frame number and
 * machine cycle so a recovered frame can seed history search (once C3/C4 land).
 *
 * Worker thread pushes; main/control read under the ring mutex.
 */

#include "display_frame.h"
#include "mutex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_FRAME_RING_DEFAULT_MEMORY_MB = 128,
    RUNTIME_FRAME_RING_MAX_MEMORY_MB = 4096,
    RUNTIME_FRAME_RING_PIXELS =
        (int)DISPLAY_FRAME_WIDTH * (int)DISPLAY_FRAME_HEIGHT
};

/* One retained completed frame (metadata + full pixel slab). */
typedef struct runtime_ring_frame {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint64_t frame_number;
    uint64_t machine_cycle;
    uint32_t pixels[RUNTIME_FRAME_RING_PIXELS];
} runtime_ring_frame;

typedef struct runtime_frame_ring {
    mutex *mutex;
    runtime_ring_frame *slots;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint64_t dropped;
    bool recording;
} runtime_frame_ring;

typedef struct runtime_frame_ring_info {
    uint32_t capacity;
    uint32_t count;
    uint64_t dropped;
    uint64_t oldest_frame;
    uint64_t newest_frame;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
    uint64_t bytes;
    bool recording;
} runtime_frame_ring_info;

bool runtime_frame_ring_init(runtime_frame_ring *ring, uint64_t budget_bytes);
void runtime_frame_ring_destroy(runtime_frame_ring *ring);
void runtime_frame_ring_clear(runtime_frame_ring *ring);
void runtime_frame_ring_set_recording(runtime_frame_ring *ring, bool recording);
void runtime_frame_ring_drop_older_than(runtime_frame_ring *ring, uint64_t cycle);

/* Push one completed live frame. pixels must be width*height ARGB. */
bool runtime_frame_ring_push(
    runtime_frame_ring *ring,
    uint64_t frame_number,
    uint64_t machine_cycle,
    uint32_t width,
    uint32_t height,
    const uint32_t *pixels);

void runtime_frame_ring_get_info(
    const runtime_frame_ring *ring,
    runtime_frame_ring_info *out_info);

/* Nearest frame at or before target; false if empty or target predates window. */
bool runtime_frame_ring_copy_by_frame(
    runtime_frame_ring *ring,
    uint64_t frame_number,
    runtime_ring_frame *out_frame);

/* Exact ring slot: index 0 = oldest retained, count-1 = newest. */
bool runtime_frame_ring_copy_by_index(
    runtime_frame_ring *ring,
    uint32_t index,
    runtime_ring_frame *out_frame);

/* Metadata only — no pixel copy. index 0 = oldest retained. */
bool runtime_frame_ring_meta_at_index(
    runtime_frame_ring *ring,
    uint32_t index,
    uint64_t *out_frame_number,
    uint64_t *out_machine_cycle);

bool runtime_frame_ring_copy_by_cycle(
    runtime_frame_ring *ring,
    uint64_t machine_cycle,
    runtime_ring_frame *out_frame);

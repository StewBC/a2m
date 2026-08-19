#include "runtime_frame_ring.h"

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

int main(void)
{
    runtime_frame_ring ring;
    runtime_ring_frame out;
    runtime_frame_ring_info info;
    uint32_t pixels[RUNTIME_FRAME_RING_PIXELS];
    uint32_t i;

    memset(pixels, 0, sizeof(pixels));
    pixels[0] = 0xFF00FF00u;
    pixels[1] = 0xFFFF0000u;

    expect_true(
        "init",
        runtime_frame_ring_init(&ring, 4ull * sizeof(runtime_ring_frame)));

    expect_true(
        "push10",
        runtime_frame_ring_push(
            &ring, 10, 1000, DISPLAY_FRAME_WIDTH, DISPLAY_FRAME_HEIGHT, pixels));
    pixels[0] = 0xFF0000FFu;
    expect_true(
        "push11",
        runtime_frame_ring_push(
            &ring, 11, 2000, DISPLAY_FRAME_WIDTH, DISPLAY_FRAME_HEIGHT, pixels));

    expect_true("copy by frame 11", runtime_frame_ring_copy_by_frame(&ring, 11, &out));
    expect_true("frame num", out.frame_number == 11);
    expect_true("pix", out.pixels[0] == 0xFF0000FFu);

    expect_true("copy by cycle", runtime_frame_ring_copy_by_cycle(&ring, 1500, &out));
    expect_true("at-or-before", out.frame_number == 10);

    expect_true("pre-window fails", !runtime_frame_ring_copy_by_frame(&ring, 1, &out));

    runtime_frame_ring_get_info(&ring, &info);
    expect_true("count", info.count == 2);
    expect_true("recording", info.recording);

    runtime_frame_ring_set_recording(&ring, false);
    pixels[0] = 0xFFFFFFFFu;
    expect_true(
        "no push when off",
        !runtime_frame_ring_push(
            &ring, 12, 3000, DISPLAY_FRAME_WIDTH, DISPLAY_FRAME_HEIGHT, pixels));

    runtime_frame_ring_clear(&ring);
    runtime_frame_ring_get_info(&ring, &info);
    expect_true("cleared", info.count == 0);

    /* Fill past capacity to exercise drop counter. */
    runtime_frame_ring_set_recording(&ring, true);
    for (i = 0; i < 8u; i++) {
        pixels[0] = i;
        (void)runtime_frame_ring_push(
            &ring,
            100u + i,
            10000u + i,
            DISPLAY_FRAME_WIDTH,
            DISPLAY_FRAME_HEIGHT,
            pixels);
    }
    runtime_frame_ring_get_info(&ring, &info);
    expect_true("capacity full", info.count == info.capacity);
    expect_true("dropped some", info.dropped > 0);

    runtime_frame_ring_destroy(&ring);
    printf("ok\n");
    return 0;
}

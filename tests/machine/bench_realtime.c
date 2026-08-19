/*
 * Real-time / turbo-zip headroom probe (not in ctest).
 *
 *   ./build/bench_realtime [seconds_emulated] [mode]
 *
 * Modes:
 *   beam   — free-run with beam pixel paint (default; historical baseline)
 *   alite  — free-run with paint_enabled=false (A-lite counters only)
 *   block  — A-lite free-run + full-frame block paint every ~1/60 s of emu time
 *            (approximates product max presentation cost on the machine path)
 *
 * Reports emulated MHz vs APPLE2_CPU_FREQUENCY_HZ. Use a Release build for
 * turbo-zip Phase 5 gates.
 */
#include "apple2.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    BENCH_MODE_BEAM = 0,
    BENCH_MODE_ALITE,
    BENCH_MODE_BLOCK
} bench_mode;

static bench_mode parse_mode(const char *s)
{
    if (s == NULL || s[0] == '\0' || strcmp(s, "beam") == 0) {
        return BENCH_MODE_BEAM;
    }
    if (strcmp(s, "alite") == 0 || strcmp(s, "max") == 0) {
        return BENCH_MODE_ALITE;
    }
    if (strcmp(s, "block") == 0) {
        return BENCH_MODE_BLOCK;
    }
    fprintf(stderr, "unknown mode '%s' (use beam|alite|block)\n", s);
    exit(2);
}

static const char *mode_name(bench_mode mode)
{
    switch (mode) {
    case BENCH_MODE_BEAM:
        return "beam";
    case BENCH_MODE_ALITE:
        return "alite";
    case BENCH_MODE_BLOCK:
        return "block";
    default:
        return "?";
    }
}

int main(int argc, char **argv)
{
    apple2_t m;
    double emu_seconds = 1.0;
    bench_mode mode = BENCH_MODE_BEAM;
    uint64_t target_cycles;
    uint64_t start_cycles;
    uint64_t next_block_cycle;
    uint64_t block_period;
    clock_t t0;
    clock_t t1;
    double wall;
    double emu_hz;
    double ratio;
    uint64_t block_paints = 0;

    if (argc > 1) {
        emu_seconds = atof(argv[1]);
        if (emu_seconds < 0.1) {
            emu_seconds = 0.1;
        }
        if (emu_seconds > 30.0) {
            emu_seconds = 30.0;
        }
    }
    if (argc > 2) {
        mode = parse_mode(argv[2]);
    }

    if (!apple2_init(&m)) {
        fprintf(stderr, "apple2_init failed\n");
        return 1;
    }

    /* HGR white screen so paint does real work. */
    m.state_flags = A2S_HIRES;
    memset(m.ram_main + 0x2000, 0x7F, 0x2000);

    if (mode == BENCH_MODE_BEAM) {
        m.video.paint_enabled = true;
    } else {
        m.video.paint_enabled = false;
    }

    block_period = (uint64_t)(APPLE2_CPU_FREQUENCY_HZ / 60.0);
    if (block_period == 0u) {
        block_period = 1u;
    }

    target_cycles = (uint64_t)(APPLE2_CPU_FREQUENCY_HZ * emu_seconds);
    start_cycles = apple2_cycles(&m);
    next_block_cycle = start_cycles + block_period;
    t0 = clock();
    while (apple2_cycles(&m) - start_cycles < target_cycles) {
        if (mode == BENCH_MODE_ALITE || mode == BENCH_MODE_BLOCK) {
            /* S2-like: instruction quanta, no video (max free-run core). */
            (void)apple2_step_instruction_max(&m);
        } else {
            (void)apple2_step_cycles(&m, 8192, NULL);
        }
        if (mode == BENCH_MODE_BLOCK) {
            while (apple2_cycles(&m) >= next_block_cycle) {
                apple2_video_paint_full_frame(&m);
                block_paints++;
                next_block_cycle += block_period;
            }
        }
    }
    t1 = clock();
    wall = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    if (wall <= 0.0) {
        wall = 1e-6;
    }
    emu_hz = (double)(apple2_cycles(&m) - start_cycles) / wall;
    ratio = emu_hz / APPLE2_CPU_FREQUENCY_HZ;

    printf("mode:     %s\n", mode_name(mode));
    printf("emulated: %.3f s (%.0f Hz target)\n", emu_seconds, APPLE2_CPU_FREQUENCY_HZ);
    printf("wall:     %.3f s\n", wall);
    printf("speed:    %.3f MHz (%.2fx real-time)\n", emu_hz / 1e6, ratio);
    if (mode == BENCH_MODE_BLOCK) {
        printf("block paints: %llu\n", (unsigned long long)block_paints);
    }
    printf(
        "verdict:  %s\n",
        ratio >= 1.0 ? "OK real-time headroom on this host" : "BELOW real-time");

    apple2_shutdown(&m);
    return ratio >= 1.0 ? 0 : 2;
}

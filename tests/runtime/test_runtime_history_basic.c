/* Minimal C3 gate: history arena + observer can record free-run instructions. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_history.h"
#include "runtime_internal.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_history_status status;
    runtime_history_record rec;
    int i;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "FAIL: SDL_Init\n");
        return 1;
    }

    runtime_config_init(&config);
    config.start_running = true;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 0;
    config.frame_ring_memory_mb_configured = true;
    expect_true("turbo", runtime_config_set_turbo_csv(&config, "max"));

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("history ptr", rt->history != NULL);
    expect_true("start", runtime_start(rt));

    /* Free-run briefly so observer records instructions. */
    SDL_Delay(50);

    runtime_history_get_status(rt->history, &status);
    expect_true("available", status.available);
    expect_true("recording", status.recording);
    expect_true("has records", status.record_count > 0);

    expect_true("last", runtime_history_last(rt->history, &rec));
    expect_true(
        "kind insn or marker",
        rec.kind == RUNTIME_HISTORY_RECORD_INSTRUCTION ||
            rec.kind == RUNTIME_HISTORY_RECORD_MARKER ||
            rec.kind == RUNTIME_HISTORY_RECORD_IRQ ||
            rec.kind == RUNTIME_HISTORY_RECORD_NMI);

    /* Walk a few newest records for ordering sanity. */
    for (i = 0; i < 8 && rec.id > 1; i++) {
        runtime_history_record prev;
        expect_true(
            "prev",
            runtime_history_previous(rt->history, rec.epoch, rec.id, &prev));
        expect_true("order", prev.id < rec.id);
        rec = prev;
    }

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok records=%llu\n", (unsigned long long)status.record_count);
    return 0;
}

/* TM2: checkpoint ring, sealed materialize to scratch, media truncate. */
#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "runtime_timemachine.h"

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

static void drain(runtime_client *client)
{
    runtime_event event;
    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

static int wait_event(
    runtime_client *client, runtime_event_type type, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == type) {
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    apple2_t scratch;
    apple2_t scratch2;
    runtime_history_status st_before;
    runtime_history_status st_after;
    runtime_tm_window window;
    uint64_t mid;
    uint16_t pc1;
    uint16_t pc2;
    uint8_t ram0;
    uint32_t size_blob = 0;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "FAIL: SDL_Init\n");
        return 1;
    }

    runtime_config_init(&config);
    config.start_running = false;
    config.timemachine = true;
    config.timemachine_memory_mb = 16;
    config.timemachine_memory_mb_configured = true;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 8;
    config.frame_ring_memory_mb_configured = true;
    config.history_off_on_max = true;
    expect_true("turbo 1", runtime_config_set_turbo_csv(&config, "1"));

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    expect_true("started", wait_event(client, RUNTIME_EVENT_STARTED, 2.0));
    expect_true("latched pause", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain(client);
    expect_true("run", runtime_client_run(client));
    expect_true("running", wait_event(client, RUNTIME_EVENT_RUNNING, 2.0));
    SDL_Delay(80);
    expect_true("pause", runtime_client_pause(client));
    expect_true("paused", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain(client);
    expect_true("worker paused", rt->exec_state != RUNTIME_EXEC_RUNNING);

    expect_true("TM on", runtime_tm_enabled(rt));
    expect_true("has CP", runtime_tm_checkpoint_count(rt) >= 1u);
    size_blob = (uint32_t)apple2_snapshot_size(&rt->machine);
    expect_true("snapshot size", size_blob > 100000u && size_blob < 400000u);
    printf("tm2 snapshot_size=%u cadence=%u cps=%llu\n",
        size_blob,
        (unsigned)RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES,
        (unsigned long long)runtime_tm_checkpoint_count(rt));

    (void)runtime_tm_checkpoint_take(rt);
    runtime_tm_window_info(rt, &window);
    expect_true("window valid", window.valid);
    mid = window.newest_cycle;
    if (mid < window.oldest_cycle || !window.valid) {
        fprintf(
            stderr,
            "window old=%llu new=%llu cps=%llu\n",
            (unsigned long long)window.oldest_cycle,
            (unsigned long long)window.newest_cycle,
            (unsigned long long)window.checkpoint_count);
    }
    expect_true("mid cycle", mid > 0u);

    expect_true("init scratch", apple2_init(&scratch));
    runtime_history_get_status(rt->history, &st_before);
    expect_true("materialize", runtime_tm_materialize(rt, mid, &scratch));
    runtime_history_get_status(rt->history, &st_after);
    if (st_before.record_count != st_after.record_count) {
        fprintf(
            stderr,
            "HST1 %llu -> %llu\n",
            (unsigned long long)st_before.record_count,
            (unsigned long long)st_after.record_count);
    }
    expect_true("seal HST1", st_before.record_count == st_after.record_count);
    expect_true("scratch cycles", apple2_cycles(&scratch) >= mid - 20000u);

    pc1 = scratch.cpu.cpu.pc;
    ram0 = apple2_debug_read(&scratch, 0x0000);

    expect_true("init scratch2", apple2_init(&scratch2));
    expect_true("materialize 2", runtime_tm_materialize(rt, mid, &scratch2));
    pc2 = scratch2.cpu.cpu.pc;
    expect_true("determinism pc", pc1 == pc2);
    expect_true(
        "determinism ram", apple2_debug_read(&scratch2, 0x0000) == ram0);
    expect_true("determinism A", scratch.cpu.cpu.A == scratch2.cpu.cpu.A);

    apple2_shutdown(&scratch);
    apple2_shutdown(&scratch2);

    /* Housekeeping flush must not truncate. */
    {
        uint64_t trunc = runtime_tm_media_truncations(rt);
        expect_true("flush media", apple2_snapshot_flush_media(&rt->machine));
        expect_true(
            "flush does not truncate",
            runtime_tm_media_truncations(rt) == trunc);
    }

    /* Off path: disable TM, CPs stop growing on further run. */
    {
        uint64_t cps = runtime_tm_checkpoint_count(rt);
        uint64_t token = runtime_client_alloc_request_token(client);
        expect_true("TM off", runtime_client_tm_set_enabled(client, false, token));
        SDL_Delay(20);
        drain(client);
        expect_true("run off", runtime_client_run(client));
        SDL_Delay(40);
        expect_true("pause off", runtime_client_pause(client));
        expect_true("paused off", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        expect_true("no new CP while off", runtime_tm_checkpoint_count(rt) == cps);
    }

    /* Media truncate: marker retained as window left edge. */
    {
        uint64_t token;
        runtime_history_record rec;
        uint64_t trunc_before = runtime_tm_media_truncations(rt);

        token = runtime_client_alloc_request_token(client);
        expect_true(
            "TM on again", runtime_client_tm_set_enabled(client, true, token));
        SDL_Delay(20);
        drain(client);
        (void)runtime_tm_checkpoint_take(rt);
        runtime_tm_on_media_event(
            rt, apple2_cycles(&rt->machine), 6, 0, APPLE2_MEDIA_EVENT_GUEST_WRITE);
        expect_true(
            "trunc count",
            runtime_tm_media_truncations(rt) > trunc_before);
        expect_true("first after cut", runtime_history_first(rt->history, &rec));
        expect_true(
            "MEDIA_CHANGED",
            rec.kind == RUNTIME_HISTORY_RECORD_MARKER &&
                rec.marker_kind == RUNTIME_HISTORY_MARKER_MEDIA_CHANGED);
        expect_true(
            "cause guest write",
            rec.marker_arg0 == RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE);
        runtime_tm_window_info(rt, &window);
        expect_true("window after cut", window.valid);
        expect_true("window oldest id is marker", window.oldest_id == rec.id);
        expect_true("start kind", window.start_kind ==
            RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE);
    }

    /* Max turbo (TMA3): remember Record, wipe tape, Record off; leave restores. */
    {
        uint64_t oldest = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;

        expect_true("run max", runtime_client_run(client));
        expect_true(
            "set max",
            runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MAX));
        expect_true("pause in max", runtime_client_pause(client));
        expect_true("paused in max", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        drain(client);
        expect_true("Record off in max", !runtime_tm_enabled(rt));
        expect_true("tape wiped in max", runtime_tm_checkpoint_count(rt) == 0u);
        runtime_tm_timeline_bounds(rt, &oldest, &live, &n);
        expect_true("no timeline in max", n == 0u);

        expect_true(
            "set 1MHz",
            runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MHZ_1));
        SDL_Delay(20);
        drain(client);
        expect_true("Record restored on leave max", runtime_tm_enabled(rt));
        expect_true(
            "fresh tape after leave max",
            runtime_tm_checkpoint_count(rt) >= 1u);
        runtime_tm_timeline_bounds(rt, &oldest, &live, &n);
        expect_true("timeline after leave max", n >= 1u);

        /* Record-off stays off across a max round-trip. */
        {
            uint64_t token = runtime_client_alloc_request_token(client);
            expect_true(
                "TM off before max",
                runtime_client_tm_set_enabled(client, false, token));
            SDL_Delay(20);
            drain(client);
            expect_true(
                "set max again",
                runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MAX));
            SDL_Delay(20);
            drain(client);
            expect_true("still off in max", !runtime_tm_enabled(rt));
            expect_true(
                "set 1MHz again",
                runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MHZ_1));
            SDL_Delay(20);
            drain(client);
            expect_true("still off after leave max", !runtime_tm_enabled(rt));
        }
    }

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

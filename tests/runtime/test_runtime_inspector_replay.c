/* Frame-aligned TimeMachine samples, sealed materialize, media cuts, max. */
#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "runtime_inspector.h"

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
    runtime_inspector_window window;
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
    config.inspector = true;
    config.inspector_memory_mb = 16;
    config.inspector_memory_mb_configured = true;
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

    expect_true("TM on", runtime_inspector_enabled(rt));
    expect_true("has CP", runtime_inspector_checkpoint_count(rt) >= 1u);
    {
        uint64_t i;
        uint64_t previous_id = 0u;
        for (i = 0u; i < runtime_inspector_sample_count(rt); i++) {
            runtime_inspector_sample_meta meta;
            runtime_ring_frame picture;
            expect_true("sample meta", runtime_inspector_sample_meta_at(rt, i, &meta));
            expect_true("stable increasing id", meta.sample_id > previous_id);
            expect_true("finite kind", meta.kind == RUNTIME_INSPECTOR_SAMPLE_FINITE_FRAME);
            expect_true("snapshot follows frame", meta.snapshot_cycle >= meta.frame_cycle);
            expect_true("paired picture id", meta.picture_id == meta.sample_id);
            expect_true(
                "exact paired picture",
                runtime_client_inspector_copy_picture(client, meta.picture_id, &picture));
            expect_true("picture captured at F", picture.machine_cycle == meta.frame_cycle);
            previous_id = meta.sample_id;
        }
    }
    {
        uint64_t before = runtime_inspector_sample_count(rt);
        expect_true("request frame", runtime_client_request_frame(client));
        SDL_Delay(20);
        drain(client);
        expect_true(
            "host-only request creates no sample",
            runtime_inspector_sample_count(rt) == before);
    }
    size_blob = (uint32_t)apple2_snapshot_size(&rt->machine);
    expect_true("snapshot size", size_blob > 100000u && size_blob < 400000u);
    printf("tm snapshot_size=%u samples=%llu\n",
        size_blob,
        (unsigned long long)runtime_inspector_checkpoint_count(rt));

    runtime_inspector_window_info(rt, &window);
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
    expect_true("materialize", runtime_inspector_materialize(rt, mid, &scratch));
    runtime_history_get_status(rt->history, &st_after);
    if (st_before.record_count != st_after.record_count) {
        fprintf(
            stderr,
            "HST1 %llu -> %llu\n",
            (unsigned long long)st_before.record_count,
            (unsigned long long)st_after.record_count);
    }
    expect_true("seal HST1", st_before.record_count == st_after.record_count);
    expect_true("scratch cycles", apple2_cycles(&scratch) == mid);

    pc1 = scratch.cpu.cpu.pc;
    ram0 = apple2_debug_read(&scratch, 0x0000);

    expect_true("init scratch2", apple2_init(&scratch2));
    expect_true("materialize 2", runtime_inspector_materialize(rt, mid, &scratch2));
    pc2 = scratch2.cpu.cpu.pc;
    expect_true("determinism pc", pc1 == pc2);
    expect_true(
        "determinism ram", apple2_debug_read(&scratch2, 0x0000) == ram0);
    expect_true("determinism A", scratch.cpu.cpu.A == scratch2.cpu.cpu.A);

    apple2_shutdown(&scratch);
    apple2_shutdown(&scratch2);

    /* Housekeeping flush must not truncate. */
    {
        uint64_t trunc = runtime_inspector_media_truncations(rt);
        expect_true("flush media", apple2_snapshot_flush_media(&rt->machine));
        expect_true(
            "flush does not truncate",
            runtime_inspector_media_truncations(rt) == trunc);
    }

    /* Off path: disable TM, CPs stop growing on further run. */
    {
        uint64_t token = runtime_client_alloc_request_token(client);
        expect_true("TM off", runtime_client_inspector_set_enabled(client, false, token));
        SDL_Delay(20);
        drain(client);
        expect_true("run off", runtime_client_run(client));
        SDL_Delay(40);
        expect_true("pause off", runtime_client_pause(client));
        expect_true("paused off", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        expect_true("window cleared while off", runtime_inspector_checkpoint_count(rt) == 0u);
    }

    /* Media truncate: marker retained as window left edge. */
    {
        uint64_t token;
        uint64_t trunc_before = runtime_inspector_media_truncations(rt);

        token = runtime_client_alloc_request_token(client);
        expect_true(
            "TM on again", runtime_client_inspector_set_enabled(client, true, token));
        SDL_Delay(20);
        drain(client);
        expect_true("run after re-enable", runtime_client_run(client));
        SDL_Delay(60);
        expect_true("pause after re-enable", runtime_client_pause(client));
        expect_true("paused after re-enable", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        expect_true("sample after re-enable", runtime_inspector_sample_count(rt) > 0u);
        runtime_inspector_on_media_event(
            rt, apple2_cycles(&rt->machine), 6, 0, APPLE2_MEDIA_EVENT_GUEST_WRITE);
        expect_true(
            "trunc count",
            runtime_inspector_media_truncations(rt) > trunc_before);
        runtime_inspector_window_info(rt, &window);
        expect_true("window hidden while waiting", !window.valid);
        expect_true("run through quiet cadences", runtime_client_run(client));
        SDL_Delay(100);
        expect_true("pause after media quiet", runtime_client_pause(client));
        expect_true("paused after media quiet", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        runtime_inspector_window_info(rt, &window);
        expect_true("window after safe anchor", window.valid);
        expect_true("start kind", window.start_kind ==
            RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE);
    }

    /* Max turbo: TimeMachine continues; default policy pauses HST1 only. */
    {
        uint64_t oldest = 0u;
        uint64_t live = 0u;
        uint64_t n = 0u;

        uint64_t before = runtime_inspector_sample_count(rt);
        expect_true(
            "set max",
            runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MAX));
        expect_true("run max", runtime_client_run(client));
        SDL_Delay(80);
        expect_true("pause in max", runtime_client_pause(client));
        expect_true("paused in max", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
        drain(client);
        expect_true("Record stays on in max", runtime_inspector_enabled(rt));
        expect_true("max adds samples", runtime_inspector_sample_count(rt) > before);
        runtime_inspector_timeline_bounds(rt, &oldest, &live, &n);
        expect_true("timeline remains in max", n > 0u);

        expect_true(
            "set 1MHz",
            runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MHZ_1));
        SDL_Delay(20);
        drain(client);
        expect_true("Record remains on after max", runtime_inspector_enabled(rt));
        runtime_inspector_timeline_bounds(rt, &oldest, &live, &n);
        expect_true("same timeline after leave max", n >= 1u);

        /* Record-off stays off across a max round-trip. */
        {
            uint64_t token = runtime_client_alloc_request_token(client);
            expect_true(
                "TM off before max",
                runtime_client_inspector_set_enabled(client, false, token));
            SDL_Delay(20);
            drain(client);
            expect_true(
                "set max again",
                runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MAX));
            SDL_Delay(20);
            drain(client);
            expect_true("still off in max", !runtime_inspector_enabled(rt));
            expect_true(
                "set 1MHz again",
                runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MHZ_1));
            SDL_Delay(20);
            drain(client);
            expect_true("still off after leave max", !runtime_inspector_enabled(rt));
        }
    }

    runtime_stop(rt);
    runtime_destroy(rt);

    /* A one-megabyte tape must evict blob-only samples without losing the
       reconstructed resume framebuffer at its new hidden anchor. */
    runtime_config_init(&config);
    config.start_running = false;
    config.inspector = true;
    config.inspector_memory_mb = 1;
    config.inspector_memory_mb_configured = true;
    config.history_memory_mb = 4;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 2;
    config.frame_ring_memory_mb_configured = true;
    expect_true("small tape turbo 1", runtime_config_set_turbo_csv(&config, "1"));
    rt = runtime_create(&config);
    expect_true("small tape create", rt != NULL);
    expect_true("small tape start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("small tape started", wait_event(client, RUNTIME_EVENT_STARTED, 2.0));
    expect_true("small tape paused", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain(client);
    expect_true("small tape run", runtime_client_run(client));
    expect_true("small tape running", wait_event(client, RUNTIME_EVENT_RUNNING, 2.0));
    SDL_Delay(180);
    expect_true("small tape pause", runtime_client_pause(client));
    expect_true("small tape pause event", wait_event(client, RUNTIME_EVENT_PAUSED, 2.0));
    expect_true("small tape evicted", runtime_inspector_checkpoints_dropped(rt) > 0u);
    {
        runtime_inspector_sample_meta oldest;
        expect_true("small tape retained sample", runtime_inspector_sample_count(rt) > 0u);
        expect_true("small tape oldest", runtime_inspector_sample_meta_at(rt, 0u, &oldest));
        expect_true("small tape scratch", apple2_init(&scratch));
        expect_true(
            "small tape reconstruct",
            runtime_inspector_materialize(rt, oldest.snapshot_cycle, &scratch));
        expect_true(
            "small tape exact cycle",
            apple2_cycles(&scratch) == oldest.snapshot_cycle);
        apple2_shutdown(&scratch);
    }
    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

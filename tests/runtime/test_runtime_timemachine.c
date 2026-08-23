/* TM0: master enable arms HST1 + frame ring; pin-3 no hidden re-arm; 0 budget. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_frame_ring.h"
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

static int wait_history_status(
    runtime_client *client,
    runtime_history_status *out,
    double timeout_s)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_HISTORY_STATUS_RESPONSE) {
                if (out != NULL) {
                    *out = event.data.history_status;
                }
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static void init_config(runtime_config *config)
{
    runtime_config_init(config);
    config->start_running = true;
    config->history_off_on_max = false;
    expect_true("turbo 1", runtime_config_set_turbo_csv(config, "1"));
}

static runtime *start_runtime(const runtime_config *config, runtime_client **out_client)
{
    runtime *rt = runtime_create(config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    *out_client = runtime_get_client(rt);
    expect_true("client", *out_client != NULL);
    SDL_Delay(40);
    drain(*out_client);
    return rt;
}

static void history_info(
    runtime_client *client,
    runtime_history_status *status)
{
    uint64_t token = runtime_client_alloc_request_token(client);
    expect_true("history-info", runtime_client_history_info(client, token));
    expect_true("history-info resp", wait_history_status(client, status, 2.0));
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_history_status status;
    runtime_frame_ring_info frame_info;
    uint64_t token;
    uint64_t records_off;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "FAIL: SDL_Init\n");
        return 1;
    }

    /* TM on at create arms both recorders. */
    init_config(&config);
    config.timemachine = true;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 8;
    config.frame_ring_memory_mb_configured = true;
    config.timemachine_memory_mb = 128;
    rt = start_runtime(&config, &client);
    {
        int waited = 0;
        while (!runtime_tm_enabled(rt) && waited < 2000) {
            SDL_Delay(1);
            waited++;
        }
    }
    expect_true("TM enabled at start", runtime_tm_enabled(rt));
    expect_true("TM budget stored", runtime_tm_memory_mb(rt) == 128u);
    history_info(client, &status);
    expect_true("history available", status.available);
    expect_true("history recording (TM on)", status.recording);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame ring capacity", frame_info.capacity > 0u);
    expect_true("frame ring recording (TM on)", frame_info.recording);

    /* Pin 3: history-record off stays off while TM stays on. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "history-record off",
        runtime_client_history_record(client, false, token));
    expect_true("off resp", wait_history_status(client, &status, 2.0));
    expect_true("history not recording", !status.recording);
    records_off = status.record_count;
    expect_true("TM still enabled after history off", runtime_tm_enabled(rt));
    SDL_Delay(40);
    history_info(client, &status);
    expect_true("history still off (no re-arm)", !status.recording);
    expect_true("history count stable off", status.record_count == records_off);

    /* Pin 3: frame-ring-record off stays off while TM stays on. */
    runtime_client_set_frame_ring_recording(client, false);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame ring off", !frame_info.recording);
    SDL_Delay(40);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame ring still off (no re-arm)", !frame_info.recording);
    expect_true("TM still enabled after frame off", runtime_tm_enabled(rt));

    runtime_stop(rt);
    runtime_destroy(rt);

    /* Off→on arms recorders that were explicitly turned off. */
    init_config(&config);
    config.timemachine = false;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 8;
    config.frame_ring_memory_mb_configured = true;
    rt = start_runtime(&config, &client);
    expect_true("TM off at start", !runtime_tm_enabled(rt));
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "standalone history off",
        runtime_client_history_record(client, false, token));
    expect_true("standalone off resp", wait_history_status(client, &status, 2.0));
    expect_true("history off before TM", !status.recording);
    runtime_client_set_frame_ring_recording(client, false);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame off before TM", !frame_info.recording);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "TM enable",
        runtime_client_tm_set_enabled(client, true, token));
    expect_true("TM enable resp", wait_history_status(client, &status, 2.0));
    expect_true("TM enabled after set", runtime_tm_enabled(rt));
    expect_true("history armed on TM on", status.recording);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame armed on TM on", frame_info.recording);

    runtime_stop(rt);
    runtime_destroy(rt);

    /* TM off does not block standalone history-record on. */
    init_config(&config);
    config.timemachine = false;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 0;
    config.frame_ring_memory_mb_configured = true;
    rt = start_runtime(&config, &client);
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "TM-off history off",
        runtime_client_history_record(client, false, token));
    expect_true("TM-off off resp", wait_history_status(client, &status, 2.0));
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "TM-off history on",
        runtime_client_history_record(client, true, token));
    expect_true("TM-off on resp", wait_history_status(client, &status, 2.0));
    expect_true("standalone history on while TM off", status.recording);
    expect_true("TM still off", !runtime_tm_enabled(rt));
    runtime_stop(rt);
    runtime_destroy(rt);

    /* Zero history budget: honest empty tape, TM still on. */
    init_config(&config);
    config.timemachine = true;
    config.history_memory_mb = 0;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 8;
    config.frame_ring_memory_mb_configured = true;
    rt = start_runtime(&config, &client);
    expect_true("TM on with history 0", runtime_tm_enabled(rt));
    history_info(client, &status);
    expect_true("history unavailable", !status.available);
    expect_true(
        "history DISABLED_BY_CONFIG",
        status.unavailable_reason == RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame ring still armed", frame_info.recording);
    runtime_stop(rt);
    runtime_destroy(rt);

    /* Zero frame-ring budget: empty ring, TM still on, history armed. */
    init_config(&config);
    config.timemachine = true;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 0;
    config.frame_ring_memory_mb_configured = true;
    rt = start_runtime(&config, &client);
    expect_true("TM on with frame 0", runtime_tm_enabled(rt));
    history_info(client, &status);
    expect_true("history armed with frame 0", status.recording);
    runtime_client_get_frame_ring_info(client, &frame_info);
    expect_true("frame capacity 0", frame_info.capacity == 0u);
    runtime_stop(rt);
    runtime_destroy(rt);

    SDL_Quit();
    printf("ok\n");
    return 0;
}

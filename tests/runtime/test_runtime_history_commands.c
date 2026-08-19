/* C4a: HISTORY_INFO / RECORD / CLEAR via runtime_client. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"

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

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_history_status status;
    uint64_t token;
    uint64_t records_before;

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
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    SDL_Delay(40);
    drain(client);

    token = runtime_client_alloc_request_token(client);
    expect_true("info", runtime_client_history_info(client, token));
    expect_true("info resp", wait_history_status(client, &status, 2.0));
    expect_true("available", status.available);
    expect_true("recording on", status.recording);
    expect_true("has records", status.record_count > 0);
    records_before = status.record_count;

    /* Stop recording: free-run should not advance newest_id. */
    token = runtime_client_alloc_request_token(client);
    expect_true("record off", runtime_client_history_record(client, false, token));
    expect_true("off resp", wait_history_status(client, &status, 2.0));
    expect_true("not recording", !status.recording);
    records_before = status.record_count;
    {
        uint64_t newest_off = status.newest_id;
        SDL_Delay(30);
        token = runtime_client_alloc_request_token(client);
        expect_true("info2", runtime_client_history_info(client, token));
        expect_true("info2 resp", wait_history_status(client, &status, 2.0));
        expect_true("stable while off", status.record_count == records_before);
        expect_true("newest stable off", status.newest_id == newest_off);

        /* Resume recording: newest_id advances (count may already be at capacity under max). */
        token = runtime_client_alloc_request_token(client);
        expect_true("record on", runtime_client_history_record(client, true, token));
        expect_true("on resp", wait_history_status(client, &status, 2.0));
        expect_true("recording again", status.recording);
        {
            uint64_t newest_before = status.newest_id;
            SDL_Delay(30);
            token = runtime_client_alloc_request_token(client);
            expect_true("info3", runtime_client_history_info(client, token));
            expect_true("info3 resp", wait_history_status(client, &status, 2.0));
            expect_true("grew after on", status.newest_id > newest_before);
        }
    }

    /* Clear starts a new epoch and drops retained records (except start marker). */
    token = runtime_client_alloc_request_token(client);
    expect_true("clear", runtime_client_history_clear(client, token));
    expect_true("clear resp", wait_history_status(client, &status, 2.0));
    expect_true("still available", status.available);
    expect_true("still recording", status.recording);
    expect_true("few after clear", status.record_count < 1000u);
    expect_true("epoch advanced", status.epoch >= 2u);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

/* C4b: HISTORY_FIND / READ / CLOSE while paused; busy while running. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_history_wire.h"

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

static int wait_history_result(
    runtime_client *client,
    uint64_t token,
    runtime_history_rpc_meta *out_meta,
    uint8_t **out_bytes,
    uint32_t *out_len,
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
            if (event.type == RUNTIME_EVENT_HISTORY_RESULT_RESPONSE &&
                event.request_token == token) {
                if (out_meta != NULL) {
                    *out_meta = event.data.history_rpc;
                }
                if (event.data.history_rpc.status != RUNTIME_HISTORY_RPC_OK) {
                    if (out_bytes != NULL) {
                        *out_bytes = NULL;
                    }
                    if (out_len != NULL) {
                        *out_len = 0;
                    }
                    return 1;
                }
                /* Status-only OK (e.g. history-close) has no pool payload. */
                if (event.data.history_rpc.byte_length == 0u) {
                    if (out_bytes != NULL) {
                        *out_bytes = NULL;
                    }
                    if (out_len != NULL) {
                        *out_len = 0;
                    }
                    return 1;
                }
                if (!runtime_client_claim_history_rpc(
                        client, token, out_bytes, out_len, out_meta)) {
                    fprintf(
                        stderr,
                        "FAIL: claim history rpc status=%d byte_length=%u count=%u\n",
                        (int)event.data.history_rpc.status,
                        event.data.history_rpc.byte_length,
                        event.data.history_rpc.count);
                    exit(1);
                }
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
    runtime_history_query query;
    runtime_history_rpc_meta meta;
    uint8_t *bytes = NULL;
    uint32_t length = 0;
    uint64_t token;
    uint64_t anchor_id = 0;
    uint64_t epoch = 0;

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

    SDL_Delay(40);
    drain(client);

    /* Find while running must fail busy. */
    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_BACKWARD;
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find while running",
        runtime_client_history_find(
            client, 0u, &query, RUNTIME_HISTORY_FROM_NEWEST, 0, 16, token));
    expect_true("busy resp", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("machine-running", meta.status == RUNTIME_HISTORY_RPC_MACHINE_RUNNING);
    expect_true("no payload", bytes == NULL);

    /* Pause then find empty filter (page of records). */
    expect_true("pause", runtime_client_pause(client));
    {
        clock_t start = clock();
        int saw_paused = 0;
        runtime_event event;
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_PAUSED) {
                    saw_paused = 1;
                }
            }
            if (saw_paused) {
                break;
            }
            SDL_Delay(1);
        }
        expect_true("saw paused", saw_paused);
    }

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find paused",
        runtime_client_history_find(
            client, 0u, &query, RUNTIME_HISTORY_FROM_NEWEST, 0, 32, token));
    expect_true("find ok", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    if (meta.status != RUNTIME_HISTORY_RPC_OK) {
        fprintf(stderr, "FAIL: find status=%d (expected OK)\n", (int)meta.status);
        exit(1);
    }
    if (bytes == NULL) {
        fprintf(
            stderr,
            "FAIL: no payload status=%d count=%u len=%u\n",
            (int)meta.status,
            meta.count,
            meta.byte_length);
        exit(1);
    }
    expect_true("payload size", length >= RUNTIME_HISTORY_WIRE_HEADER_SIZE);
    expect_true("count", meta.count > 0);
    expect_true("HST1", length >= 4 && memcmp(bytes, "HST1", 4) == 0);
    epoch = meta.epoch;
    anchor_id = meta.newest;
    free(bytes);
    bytes = NULL;

    /* Read context around newest. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "read",
        runtime_client_history_read(client, 0u, epoch, anchor_id, 8, 4, token));
    expect_true("read ok", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("read status", meta.status == RUNTIME_HISTORY_RPC_OK);
    expect_true("read payload", bytes != NULL && meta.count >= 1);
    free(bytes);
    bytes = NULL;

    /* Close is always ok. */
    token = runtime_client_alloc_request_token(client);
    expect_true("close", runtime_client_history_close(client, 0u, 0, token));
    expect_true("close ok", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("close status", meta.status == RUNTIME_HISTORY_RPC_OK);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

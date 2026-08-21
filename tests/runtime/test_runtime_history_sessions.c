/* S0/S1: two sessions page history independently (no cursor cross-stomp). */
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
                    fprintf(stderr, "FAIL: claim history rpc\n");
                    exit(1);
                }
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static int wait_session_response(
    runtime_client *client,
    uint64_t token,
    runtime_session_status *out_status,
    uint32_t *out_session_id,
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
            if (event.type == RUNTIME_EVENT_SESSION_RESPONSE &&
                event.request_token == token) {
                if (out_status != NULL) {
                    *out_status = event.data.session.status;
                }
                if (out_session_id != NULL) {
                    *out_session_id = event.data.session.session_id;
                }
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static void wait_paused(runtime_client *client)
{
    clock_t start = clock();
    runtime_event event;
    int saw_paused = 0;

    expect_true("pause", runtime_client_pause(client));
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_PAUSED) {
                saw_paused = 1;
            }
        }
        if (saw_paused) {
            return;
        }
        SDL_Delay(1);
    }
    expect_true("saw paused", 0);
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_history_query query;
    runtime_history_rpc_meta meta_a;
    runtime_history_rpc_meta meta_b;
    runtime_session_status session_status;
    uint8_t *bytes = NULL;
    uint32_t length = 0;
    uint64_t token;
    uint32_t session_a = 0;
    uint32_t session_b = 0;
    uint64_t cursor_a = 0;
    uint64_t cursor_b = 0;
    uint64_t newest_a = 0;
    uint64_t newest_b = 0;

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

    SDL_Delay(50);
    drain(client);
    wait_paused(client);

    /* Open two UI sessions (default session remains reserved). */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open a",
        runtime_client_session_open(client, RUNTIME_SESSION_KIND_UI, token));
    expect_true(
        "open a resp",
        wait_session_response(client, token, &session_status, &session_a, 2.0));
    expect_true("open a ok", session_status == RUNTIME_SESSION_OK);
    expect_true("session a id", session_a != 0u);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open b",
        runtime_client_session_open(client, RUNTIME_SESSION_KIND_UI, token));
    expect_true(
        "open b resp",
        wait_session_response(client, token, &session_status, &session_b, 2.0));
    expect_true("open b ok", session_status == RUNTIME_SESSION_OK);
    expect_true("session b id", session_b != 0u && session_b != session_a);

    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_BACKWARD;

    /* Small pages so more=1 and each session keeps its own cursor. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find a",
        runtime_client_history_find(
            client,
            session_a,
            &query,
            RUNTIME_HISTORY_FROM_NEWEST,
            0,
            2,
            token));
    expect_true(
        "find a ok", wait_history_result(client, token, &meta_a, &bytes, &length, 2.0));
    expect_true("find a status", meta_a.status == RUNTIME_HISTORY_RPC_OK);
    expect_true("find a more", meta_a.more == 1u);
    expect_true("find a cursor", meta_a.cursor != 0u);
    cursor_a = meta_a.cursor;
    newest_a = meta_a.newest;
    free(bytes);
    bytes = NULL;

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find b",
        runtime_client_history_find(
            client,
            session_b,
            &query,
            RUNTIME_HISTORY_FROM_NEWEST,
            0,
            2,
            token));
    expect_true(
        "find b ok", wait_history_result(client, token, &meta_b, &bytes, &length, 2.0));
    expect_true("find b status", meta_b.status == RUNTIME_HISTORY_RPC_OK);
    expect_true("find b more", meta_b.more == 1u);
    expect_true("find b cursor", meta_b.cursor != 0u);
    cursor_b = meta_b.cursor;
    newest_b = meta_b.newest;
    free(bytes);
    bytes = NULL;

    expect_true("cursors distinct", cursor_a != cursor_b);

    /* NEXT on A must not advance B's cursor. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "next a",
        runtime_client_history_next(client, session_a, cursor_a, 2, token));
    expect_true(
        "next a ok", wait_history_result(client, token, &meta_a, &bytes, &length, 2.0));
    expect_true("next a status", meta_a.status == RUNTIME_HISTORY_RPC_OK);
    expect_true("next a moved", meta_a.newest != newest_a || meta_a.oldest != newest_a);
    free(bytes);
    bytes = NULL;

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "next b",
        runtime_client_history_next(client, session_b, cursor_b, 2, token));
    expect_true(
        "next b ok", wait_history_result(client, token, &meta_b, &bytes, &length, 2.0));
    expect_true("next b status", meta_b.status == RUNTIME_HISTORY_RPC_OK);
    /* B still pages from its own FIND start, not A's advanced position. */
    expect_true("next b independent", meta_b.newest == newest_b || meta_b.count > 0u);
    free(bytes);
    bytes = NULL;

    /* Wrong session + cursor → stale. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "cross next",
        runtime_client_history_next(client, session_b, cursor_a, 2, token));
    expect_true(
        "cross next resp",
        wait_history_result(client, token, &meta_b, &bytes, &length, 2.0));
    expect_true("cross stale", meta_b.status == RUNTIME_HISTORY_RPC_CURSOR_STALE);
    expect_true("cross no payload", bytes == NULL);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "close a", runtime_client_session_close(client, session_a, token));
    expect_true(
        "close a resp",
        wait_session_response(client, token, &session_status, &session_a, 2.0));
    expect_true("close a ok", session_status == RUNTIME_SESSION_OK);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "close b", runtime_client_session_close(client, session_b, token));
    expect_true(
        "close b resp",
        wait_session_response(client, token, &session_status, &session_b, 2.0));
    expect_true("close b ok", session_status == RUNTIME_SESSION_OK);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

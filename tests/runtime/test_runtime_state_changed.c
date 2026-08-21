/* S3: state-changed inform + cursor stale after mutation. */
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
    uint32_t *out_session_id,
    double timeout_s)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_SESSION_RESPONSE &&
                event.request_token == token) {
                expect_true("session ok", event.data.session.status == RUNTIME_SESSION_OK);
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

static int wait_state_changed(
    runtime_client *client,
    runtime_state_changed_reason expect_reason,
    uint32_t expect_source,
    double timeout_s)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_STATE_CHANGED) {
                expect_true(
                    "reason",
                    event.data.state_changed.reason == expect_reason);
                expect_true(
                    "source",
                    event.data.state_changed.source_session_id == expect_source);
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
    int saw = 0;

    expect_true("pause cmd", runtime_client_pause(client));
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_PAUSED) {
                saw = 1;
            }
        }
        if (saw) {
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
    runtime_history_rpc_meta meta;
    uint8_t *bytes = NULL;
    uint32_t length = 0;
    uint64_t token;
    uint32_t session_a = 0;
    uint32_t session_b = 0;
    uint64_t cursor = 0;

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
    wait_paused(client);
    drain(client);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open a",
        runtime_client_session_open(client, RUNTIME_SESSION_KIND_UI, 0u, token));
    expect_true("open a resp", wait_session_response(client, token, &session_a, 2.0));

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "open b",
        runtime_client_session_open(client, RUNTIME_SESSION_KIND_UI, 0u, token));
    expect_true("open b resp", wait_session_response(client, token, &session_b, 2.0));
    expect_true("distinct", session_a != session_b);

    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_BACKWARD;
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "find b",
        runtime_client_history_find(
            client, session_b, &query, RUNTIME_HISTORY_FROM_NEWEST, 0, 2, token));
    expect_true(
        "find b ok", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("find more", meta.more == 1u && meta.cursor != 0u);
    cursor = meta.cursor;
    free(bytes);
    bytes = NULL;

    /* Session A steps → observers see state-changed(step). */
    runtime_client_set_command_session(client, session_a);
    drain(client);
    expect_true("step", runtime_client_step_instruction(client));
    expect_true(
        "state-changed",
        wait_state_changed(client, RUNTIME_STATE_CHANGED_STEP, session_a, 2.0));

    /* B's cursor is stale after the mutation. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "next stale",
        runtime_client_history_next(client, session_b, cursor, 2, token));
    expect_true(
        "next stale resp",
        wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("stale", meta.status == RUNTIME_HISTORY_RPC_CURSOR_STALE);

    /* Re-FIND on B works after stale. */
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "refind",
        runtime_client_history_find(
            client, session_b, &query, RUNTIME_HISTORY_FROM_NEWEST, 0, 2, token));
    expect_true(
        "refind ok", wait_history_result(client, token, &meta, &bytes, &length, 2.0));
    expect_true("refind status", meta.status == RUNTIME_HISTORY_RPC_OK);
    free(bytes);

    runtime_client_set_command_session(client, 0u);
    token = runtime_client_alloc_request_token(client);
    (void)runtime_client_session_close(client, session_a, token);
    token = runtime_client_alloc_request_token(client);
    (void)runtime_client_session_close(client, session_b, token);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

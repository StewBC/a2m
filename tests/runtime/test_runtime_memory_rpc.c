#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s: expected true\n", name);
        exit(1);
    }
}

static int poll_event(
    runtime_client *client,
    runtime_event *event,
    runtime_event_type type,
    double timeout_s)
{
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
    }
    return 0;
}

static void drain_to_paused(runtime_client *client)
{
    runtime_event event;
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_PAUSED) {
                return;
            }
        }
    }
    fail("timeout waiting for PAUSED");
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint64_t token;
    uint8_t *bytes = NULL;
    uint32_t length = 0;
    uint16_t address = 0;
    runtime_memory_mode mode = RUNTIME_MEMORY_MODE_MAP;
    uint8_t poke[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t verify[4];

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init failed");
    }

    runtime_config_init(&config);
    config.start_running = false;
    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);

    expect_true("STARTED", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    drain_to_paused(client);

    /* Write four bytes via map mode. */
    expect_true(
        "write_memory",
        runtime_client_write_memory(client, 0x0300, 4, RUNTIME_MEMORY_MODE_MAP, poke));
    /* Small settle: request memory inline */
    expect_true(
        "request_memory_inline",
        runtime_client_request_memory(client, 0x0300, 4, RUNTIME_MEMORY_MODE_MAP));
    expect_true(
        "MEMORY_RESPONSE",
        poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0));
    expect_true("inline length", event.data.memory.length == 4);
    if (memcmp(event.data.memory.bytes, poke, 4) != 0) {
        fail("inline memory mismatch");
    }

    token = runtime_client_alloc_request_token(client);
    expect_true("token", token != 0);
    expect_true(
        "request_memory_token",
        runtime_client_request_memory_token(
            client, 0x0300, 4, RUNTIME_MEMORY_MODE_MAP, token));
    expect_true(
        "MEMORY_RPC",
        poll_event(client, &event, RUNTIME_EVENT_MEMORY_RPC_COMPLETE, 2.0));
    expect_true("token echo", event.request_token == token);
    expect_true("rpc ok", event.data.memory_rpc.status == RUNTIME_MEMORY_RPC_OK);
    expect_true(
        "claim",
        runtime_client_claim_memory_rpc(
            client, token, &bytes, &length, &address, &mode));
    expect_true("claim length", length == 4 && bytes != NULL);
    if (memcmp(bytes, poke, 4) != 0) {
        free(bytes);
        fail("rpc memory mismatch");
    }
    free(bytes);

    /* Main-bank mode read of same addresses (low RAM is main). */
    expect_true(
        "request_main",
        runtime_client_request_memory(client, 0x0300, 4, RUNTIME_MEMORY_MODE_MAIN));
    expect_true(
        "MEMORY_RESPONSE main",
        poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0));
    memcpy(verify, event.data.memory.bytes, 4);
    if (memcmp(verify, poke, 4) != 0) {
        fail("main memory mismatch");
    }

    expect_true("quit", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

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

static void drain_events(runtime_client *client)
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
    runtime_event event;
    uint16_t pc_saved;
    uint64_t cycles_saved;
    const char *path = "test_runtime_savestate.a2state";
    uint8_t marker[4] = { 0xCA, 0xFE, 0xBA, 0xBE };
    const uint16_t marker_addr = 0x6000;

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
    expect_true("CPU", poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    drain_events(client);

    expect_true(
        "write",
        runtime_client_write_memory(
            client, marker_addr, 4, RUNTIME_MEMORY_MODE_MAIN, marker));
    /* Allow write to apply on worker. */
    expect_true("run_some", runtime_client_run_cycles(client, 1));
    expect_true("RUN_COMPLETE", poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 2.0));
    drain_events(client);

    expect_true(
        "req_before",
        runtime_client_request_memory(client, marker_addr, 4, RUNTIME_MEMORY_MODE_MAIN));
    expect_true("MEM_before", poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0));
    if (memcmp(event.data.memory.bytes, marker, 4) != 0) {
        fail("RAM marker not written before save");
    }

    expect_true("req_cpu", runtime_client_request_cpu_state(client));
    expect_true("CPU2", poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    pc_saved = event.data.cpu_state.pc;
    cycles_saved = event.data.cpu_state.cycles;

    expect_true("save", runtime_client_save_state(client, path));
    expect_true(
        "SAVE_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_SAVE_STATE_COMPLETE, 5.0));
    drain_events(client);

    /* Mutate state then reload. */
    expect_true(
        "clobber",
        runtime_client_write_memory(
            client, marker_addr, 4, RUNTIME_MEMORY_MODE_MAIN, (const uint8_t *)"xxxx"));
    expect_true("run_more", runtime_client_run_cycles(client, 2000));
    expect_true("RUN_COMPLETE2", poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 2.0));
    drain_events(client);

    expect_true("load", runtime_client_load_state(client, path));
    expect_true(
        "LOAD_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_LOAD_STATE_COMPLETE, 5.0));

    expect_true("CPU after load", poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    if (event.data.cpu_state.pc != pc_saved) {
        fprintf(
            stderr,
            "FAIL: pc %04x != saved %04x\n",
            event.data.cpu_state.pc,
            pc_saved);
        exit(1);
    }
    if (event.data.cpu_state.cycles != cycles_saved) {
        fprintf(
            stderr,
            "FAIL: cycles %llu != saved %llu\n",
            (unsigned long long)event.data.cpu_state.cycles,
            (unsigned long long)cycles_saved);
        exit(1);
    }
    drain_events(client);

    expect_true(
        "mem",
        runtime_client_request_memory(client, marker_addr, 4, RUNTIME_MEMORY_MODE_MAIN));
    expect_true("MEM", poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0));
    if (memcmp(event.data.memory.bytes, marker, 4) != 0) {
        fprintf(
            stderr,
            "FAIL: RAM got %02x %02x %02x %02x\n",
            event.data.memory.bytes[0],
            event.data.memory.bytes[1],
            event.data.memory.bytes[2],
            event.data.memory.bytes[3]);
        fail("RAM marker not restored");
    }

    remove(path);
    expect_true("quit", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

/* TMA2: one breakpoint list; time-travel F12 / run-until hits it (or live). */
#include "apple2.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
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
    }
}

static int wait_event_type(
    runtime_client *client, runtime_event_type type, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
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
    runtime_breakpoint_definition def;
    uint64_t token;
    uint16_t target_pc;
    uint64_t old = 0u;
    uint64_t live = 0u;
    uint64_t n = 0u;
    size_t live_count;

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
    expect_true("turbo 1", runtime_config_set_turbo_csv(&config, "1"));

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);
    expect_true("started", wait_event_type(client, RUNTIME_EVENT_STARTED, 2.0));
    expect_true("paused0", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain(client);

    expect_true("run", runtime_client_run(client));
    expect_true("running", wait_event_type(client, RUNTIME_EVENT_RUNNING, 2.0));
    SDL_Delay(150);
    expect_true("pause", runtime_client_pause(client));
    expect_true("paused", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain(client);

    target_pc = rt->machine.cpu.cpu.pc;
    token = runtime_client_alloc_request_token(client);
    expect_true("enter", runtime_client_tm_enter_forensic(client, token));
    {
        clock_t t0 = clock();
        while (!runtime_tm_in_forensic(rt) &&
            (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
            SDL_Delay(1);
        }
    }
    expect_true("forensic", runtime_tm_in_forensic(rt));
    runtime_tm_timeline_bounds(rt, &old, &live, &n);
    expect_true("timeline", n >= 1u);

    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = target_pc;
    def.end_address = target_pc;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    expect_true("bp create", runtime_client_create_breakpoint(client, &def));
    SDL_Delay(20);
    drain(client);
    live_count = rt->breakpoint_count;
    expect_true("one list has bp", live_count >= 1u);

    token = runtime_client_alloc_request_token(client);
    if (live > old + 4000u) {
        old = live - 4000u;
    }
    expect_true("land near live", runtime_client_tm_land(client, old, token));
    {
        clock_t t0 = clock();
        while (apple2_cycles(&rt->machine) > old &&
               (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
            SDL_Delay(1);
        }
    }
    drain(client);
    expect_true("landed before live", apple2_cycles(&rt->machine) <= old);
    expect_true("not at live after land", !runtime_tm_at_live(rt));

    expect_true("run-until", runtime_client_run(client));
    {
        clock_t t0 = clock();
        while (rt->exec_state == RUNTIME_EXEC_RUNNING &&
               (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
            SDL_Delay(1);
        }
    }
    drain(client);
    expect_true("stopped", rt->exec_state != RUNTIME_EXEC_RUNNING);
    expect_true("still time travel", runtime_tm_in_forensic(rt));
    expect_true("hit pc or live",
        rt->machine.cpu.cpu.pc == target_pc || runtime_tm_at_live(rt));
    expect_true("list unchanged", rt->breakpoint_count == live_count);

    token = runtime_client_alloc_request_token(client);
    expect_true("leave", runtime_client_tm_exit_forensic(client, token));
    {
        clock_t t0 = clock();
        while (runtime_tm_in_forensic(rt) &&
               (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
            SDL_Delay(1);
        }
    }
    expect_true("left inspect", !runtime_tm_in_forensic(rt));
    expect_true("list survives leave", rt->breakpoint_count == live_count);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

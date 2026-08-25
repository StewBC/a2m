/* TMA2: one breakpoint list; time-travel F12 / run-until hits it (or live). */
#include "apple2.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_internal.h"
#include "runtime_inspector.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    runtime_client *client, runtime_event_type type, Uint32 timeout_ms)
{
    Uint32 start = SDL_GetTicks();
    runtime_event event;
    while ((SDL_GetTicks() - start) < timeout_ms) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == type) {
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static int wait_inspector_mode(
    runtime_client *client, uint64_t token, Uint32 timeout_ms)
{
    Uint32 start = SDL_GetTicks();
    runtime_event event;
    while ((SDL_GetTicks() - start) < timeout_ms) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_INSPECTOR_MODE &&
                event.request_token == token) {
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static int wait_state_changed(
    runtime_client *client,
    runtime_state_changed_reason reason,
    Uint32 timeout_ms)
{
    Uint32 start = SDL_GetTicks();
    runtime_event event;
    while ((SDL_GetTicks() - start) < timeout_ms) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_STATE_CHANGED &&
                event.data.state_changed.reason == reason) {
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
    config.inspector = true;
    config.inspector_memory_mb = 16;
    config.inspector_memory_mb_configured = true;
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
    expect_true("started", wait_event_type(client, RUNTIME_EVENT_STARTED, 2000u));
    expect_true("paused0", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2000u));
    drain(client);

    expect_true("run", runtime_client_run(client));
    expect_true("running", wait_event_type(client, RUNTIME_EVENT_RUNNING, 2000u));
    SDL_Delay(150);
    expect_true("pause", runtime_client_pause(client));
    expect_true("paused", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2000u));
    drain(client);

    target_pc = rt->machine.cpu.cpu.pc;
    token = runtime_client_alloc_request_token(client);
    expect_true("enter", runtime_client_inspector_enter(client, token));
    expect_true("enter mode", wait_inspector_mode(client, token, 2000u));
    expect_true("inspecting", runtime_inspector_inspecting(rt));
    runtime_inspector_timeline_bounds(rt, &old, &live, &n);
    expect_true("timeline", n >= 1u);

    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = target_pc;
    def.end_address = target_pc;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    expect_true("bp create", runtime_client_create_breakpoint(client, &def));
    expect_true(
        "bp list",
        wait_event_type(client, RUNTIME_EVENT_BREAKPOINTS_RESPONSE, 2000u));
    live_count = rt->breakpoint_count;
    expect_true("one list has bp", live_count >= 1u);

    token = runtime_client_alloc_request_token(client);
    if (live > old + 4000u) {
        old = live - 4000u;
    }
    expect_true("land near live", runtime_client_inspector_land(client, old, token));
    expect_true(
        "landed",
        wait_state_changed(client, RUNTIME_STATE_CHANGED_INSPECTOR_LAND, 2000u));
    expect_true("landed before live", apple2_cycles(&rt->machine) <= old);
    expect_true("not at live after land", !runtime_inspector_at_live(rt));

    /* RUN is async: wait for RUNNING first. Polling exec_state alone races —
     * the wait can exit while still paused from before the command lands. */
    expect_true("run-until", runtime_client_run(client));
    expect_true(
        "run-until running",
        wait_event_type(client, RUNTIME_EVENT_RUNNING, 2000u));
    expect_true(
        "run-until stopped",
        wait_event_type(client, RUNTIME_EVENT_PAUSED, 2000u));
    expect_true("stopped", rt->exec_state != RUNTIME_EXEC_RUNNING);
    expect_true("still time travel", runtime_inspector_inspecting(rt));
    expect_true("hit pc or live",
        rt->machine.cpu.cpu.pc == target_pc || runtime_inspector_at_live(rt));
    expect_true("list unchanged", rt->breakpoint_count == live_count);

    token = runtime_client_alloc_request_token(client);
    expect_true("leave", runtime_client_inspector_leave(client, token));
    expect_true("leave mode", wait_inspector_mode(client, token, 2000u));
    expect_true("left inspect", !runtime_inspector_inspecting(rt));
    expect_true("list survives leave", rt->breakpoint_count == live_count);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

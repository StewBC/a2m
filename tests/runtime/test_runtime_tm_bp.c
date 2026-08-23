/* TM5: forensic BP store is separate; tape run-until hits exec PC; live list unchanged. */
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

static int wait_tm_focus(
    runtime_client *client, uint64_t token, runtime_event *out, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_TM_FOCUS &&
                event.request_token == token) {
                if (out != NULL) {
                    *out = event;
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
    runtime_tm_window window;
    runtime_event ev;
    runtime_breakpoint_definition def;
    uint64_t token;
    uint16_t target_pc;
    size_t live_count;
    uint32_t live_id;

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

    expect_true("live exec", runtime_client_set_execute_breakpoint(client, 0xFF00));
    SDL_Delay(20);
    drain(client);
    live_count = rt->breakpoint_count;
    expect_true("live has one", live_count == 1u);
    live_id = rt->breakpoints[0].id;

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
    runtime_tm_window_info(rt, &window);
    expect_true("window", window.valid);

    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = target_pc;
    def.end_address = target_pc;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    expect_true("tm bp create", runtime_client_tm_bp_create(client, &def));
    SDL_Delay(20);
    drain(client);
    expect_true("tm store has one", runtime_tm_bp_count(rt) == 1u);
    expect_true("live count after tm create", rt->breakpoint_count == live_count);
    expect_true("live id after tm create", rt->breakpoints[0].id == live_id);

    token = runtime_client_alloc_request_token(client);
    expect_true(
        "seek oldest",
        runtime_client_tm_seek_cycle(client, window.oldest_cycle, token));
    expect_true("seek event", wait_tm_focus(client, token, &ev, 2.0));
    expect_true("seek ok", ev.data.tm_focus.status == RUNTIME_TM_QUERY_OK);

    token = runtime_client_alloc_request_token(client);
    expect_true("run until", runtime_client_tm_run_until_break(client, token));
    expect_true("until event", wait_tm_focus(client, token, &ev, 2.0));
    expect_true("until ok", ev.data.tm_focus.status == RUNTIME_TM_QUERY_OK);
    expect_true("hit pc", ev.data.tm_focus.focus.pc == target_pc);
    expect_true("live count after hit", rt->breakpoint_count == live_count);
    expect_true("live id after hit", rt->breakpoints[0].id == live_id);
    expect_true("tm store still one", runtime_tm_bp_count(rt) == 1u);

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

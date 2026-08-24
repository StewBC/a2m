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

static void fill_default_machine_config(runtime_machine_config *mc)
{
    memset(mc, 0, sizeof(*mc));
    mc->apple_model = 0u;
    mc->slot_cards[4] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
    mc->slot_cards[6] = RUNTIME_SLOT_CARD_DISKII;
    mc->slot_cards[7] = RUNTIME_SLOT_CARD_SMARTPORT;
}

static int poll_machine_without_reset(
    runtime_client *client,
    runtime_event *event,
    double timeout_s)
{
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == RUNTIME_EVENT_RESET_COMPLETE) {
                return 0;
            }
            if (event->type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                return 1;
            }
        }
    }
    return 0;
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t turbo_after_cycle = 0;
    uint32_t milli = 0;
    char label[32];

    /* Token parse unit checks. */
    expect_true("parse 1", runtime_turbo_parse_token("1", &milli) && milli == 1000u);
    expect_true("parse 2.5", runtime_turbo_parse_token("2.5", &milli) && milli == 2500u);
    expect_true("parse max", runtime_turbo_parse_token("max", &milli) && milli == RUNTIME_TURBO_MAX);
    expect_true("parse -1", runtime_turbo_parse_token("-1", &milli) && milli == RUNTIME_TURBO_MAX);
    expect_true("parse bad", !runtime_turbo_parse_token("warp", &milli));
    expect_true("parse 0", !runtime_turbo_parse_token("0", &milli));
    runtime_turbo_format_label(1000u, label, sizeof(label));
    expect_true("label 1", strcmp(label, "1 MHz") == 0);
    runtime_turbo_format_label(RUNTIME_TURBO_MAX, label, sizeof(label));
    expect_true("label max", strcmp(label, "max") == 0);
    runtime_turbo_format_token(2500u, label, sizeof(label));
    expect_true("token 2.5", strcmp(label, "2.5") == 0);

    /* CSV parse unit checks (no thread). */
    runtime_config_init(&config);
    expect_true("default count", config.turbo_speed_count == 2);
    expect_true("default active", config.active_turbo_multiplier == RUNTIME_TURBO_MHZ_1);
    expect_true("default second max", config.turbo_speeds[1] == RUNTIME_TURBO_MAX);
    expect_true("csv 1,max", runtime_config_set_turbo_csv(&config, "1,max"));
    expect_true("csv active first", config.active_turbo_multiplier == RUNTIME_TURBO_MHZ_1);
    expect_true("csv count 2", config.turbo_speed_count == 2);
    expect_true("csv 1,4,8,max", runtime_config_set_turbo_csv(&config, "1,4,8,max"));
    expect_true("csv zip count", config.turbo_speed_count == 4);
    expect_true("csv 4mhz", config.turbo_speeds[1] == 4000u);
    expect_true("csv 8mhz", config.turbo_speeds[2] == 8000u);
    expect_true("csv max entry", config.turbo_speeds[3] == RUNTIME_TURBO_MAX);
    expect_true("csv 2.5,max", runtime_config_set_turbo_csv(&config, "2.5,max"));
    expect_true("csv starts 2.5", config.active_turbo_multiplier == 2500u);
    expect_true("bad csv rejected", !runtime_config_set_turbo_csv(&config, "1,warp,3"));
    expect_true("bad resets default", config.active_turbo_multiplier == RUNTIME_TURBO_MHZ_1);
    expect_true("target hz 1", runtime_turbo_target_hz(1000u) > 1000000.0);
    expect_true("target hz max", runtime_turbo_target_hz(RUNTIME_TURBO_MAX) == 0.0);

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init failed");
    }

    runtime_config_init(&config);
    config.start_running = false;
    expect_true("set ladder", runtime_config_set_turbo_csv(&config, "1,4,max"));
    expect_true("csv count", config.turbo_speed_count == 3);

    rt = runtime_create(&config);
    expect_true("runtime_create", rt != NULL);
    expect_true("runtime_start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    expect_true("STARTED", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    /* Drain boot telemetry so later MACHINE_STATE polls match the command. */
    drain_events(client);
    SDL_Delay(20);
    drain_events(client);

    expect_true("request state", runtime_client_request_machine_state(client));
    expect_true(
        "machine state",
        poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    expect_true(
        "turbo starts 1MHz",
        event.data.machine_state.active_turbo_multiplier == RUNTIME_TURBO_MHZ_1);
    expect_true("turbo list count", event.data.machine_state.turbo_speed_count == 3u);
    drain_events(client);

    expect_true("cycle turbo", runtime_client_cycle_turbo_speed(client));
    expect_true(
        "state after cycle",
        poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    turbo_after_cycle = event.data.machine_state.active_turbo_multiplier;
    expect_true("cycled to 4MHz", turbo_after_cycle == 4000u);
    drain_events(client);

    expect_true("set max", runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MAX));
    expect_true(
        "state after set",
        poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    expect_true(
        "max mode",
        event.data.machine_state.active_turbo_multiplier == RUNTIME_TURBO_MAX);

    expect_true("set 8MHz", runtime_client_set_turbo_multiplier(client, 8000u));
    expect_true(
        "state after 8",
        poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    expect_true(
        "8MHz mode",
        event.data.machine_state.active_turbo_multiplier == 8000u);

    /* Configure OK live-applies the ladder without a machine reset. */
    {
        runtime_machine_config machine_config;
        runtime_config ladder;

        fill_default_machine_config(&machine_config);
        runtime_config_init(&ladder);
        expect_true(
            "parse live ladder",
            runtime_config_set_turbo_csv(&ladder, "1,4,8,max"));
        drain_events(client);
        expect_true(
            "apply live ladder",
            runtime_client_apply_machine_config(
                client,
                &machine_config,
                &ladder,
                NULL,
                NULL,
                false,
                false,
                false));
        expect_true(
            "ladder apply no reset",
            poll_machine_without_reset(client, &event, 2.0));
        expect_true("live count 4", event.data.machine_state.turbo_speed_count == 4u);
        expect_true(
            "keep 8MHz if still listed",
            event.data.machine_state.active_turbo_multiplier == 8000u);
        drain_events(client);

        expect_true("cycle after live apply", runtime_client_cycle_turbo_speed(client));
        expect_true(
            "state after live cycle",
            poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
        expect_true(
            "cycled 8 to max",
            event.data.machine_state.active_turbo_multiplier == RUNTIME_TURBO_MAX);
        drain_events(client);

        runtime_config_init(&ladder);
        expect_true(
            "parse shrink ladder",
            runtime_config_set_turbo_csv(&ladder, "1,max"));
        expect_true(
            "apply shrink ladder",
            runtime_client_apply_machine_config(
                client,
                &machine_config,
                &ladder,
                NULL,
                NULL,
                false,
                false,
                false));
        expect_true(
            "shrink apply no reset",
            poll_machine_without_reset(client, &event, 2.0));
        expect_true("shrink count 2", event.data.machine_state.turbo_speed_count == 2u);
        expect_true(
            "keep max when still listed",
            event.data.machine_state.active_turbo_multiplier == RUNTIME_TURBO_MAX);
        drain_events(client);

        expect_true("set 4MHz off list", runtime_client_set_turbo_multiplier(client, 4000u));
        expect_true(
            "state after off-list 4",
            poll_event(client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
        drain_events(client);
        expect_true(
            "apply drop off-list",
            runtime_client_apply_machine_config(
                client,
                &machine_config,
                &ladder,
                NULL,
                NULL,
                false,
                false,
                false));
        expect_true(
            "drop apply no reset",
            poll_machine_without_reset(client, &event, 2.0));
        expect_true(
            "off-list falls to first",
            event.data.machine_state.active_turbo_multiplier == RUNTIME_TURBO_MHZ_1);
    }

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("OK runtime_turbo\n");
    return 0;
}

#pragma once

#include "control_deferred.h"
#include "control_protocol.h"
#include "control_server.h"
#include "runtime_client.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct control_dispatch {
    control_server_t *server;
    runtime_client *client;
    deferred_control_table deferred;
    /* Sticky execution-state for waits / get-state. */
    bool seen_paused;
    bool machine_running;
    uint64_t frame_number;
    uint64_t cycle;
    uint16_t last_pc;
    uint8_t last_a;
    uint8_t last_x;
    uint8_t last_y;
    uint8_t last_sp;
    uint8_t last_p;
    bool has_cpu;
    uint32_t turbo_mode;
    char stop_reason[32];
    /* Sticky event latches (name tokens, cleared on consume / exec control). */
    bool latch_paused;
    bool latch_running;
    bool latch_step_complete;
    bool latch_run_complete;
    bool latch_reset_complete;
    bool latch_breakpoints;
    bool latch_frame;
} control_dispatch_t;

void control_dispatch_init(
    control_dispatch_t *disp,
    control_server_t *server,
    runtime_client *client);

void control_dispatch_shutdown(control_dispatch_t *disp);

/* Poll runtime events; complete deferred when matched. */
void control_dispatch_on_runtime_event(
    control_dispatch_t *disp,
    const runtime_event *event);

/* Poll one control request and handle or defer. */
void control_dispatch_poll(control_dispatch_t *disp);

/* Cancel deferred on disconnect / epoch change / timeout. */
void control_dispatch_check_session(control_dispatch_t *disp);

#pragma once

#include "control_protocol.h"

#include <stdbool.h>
#include <stdint.h>

/* C0: single exclusive deferred slot (c64m multi-table comes with history bulk). */
enum { CONTROL_DEFERRED_CAPACITY = 1 };

typedef enum control_deferred_kind {
    CONTROL_DEFERRED_NONE = 0,
    CONTROL_DEFERRED_GET_CPU,
    CONTROL_DEFERRED_GET_SOFTSWITCHES,
    CONTROL_DEFERRED_GET_MEMORY,
    CONTROL_DEFERRED_GET_FRAME,
    CONTROL_DEFERRED_BREAK_LIST,
    CONTROL_DEFERRED_WAIT_PAUSED,
    CONTROL_DEFERRED_WAIT_RUNNING,
    CONTROL_DEFERRED_WAIT_FRAME,
    CONTROL_DEFERRED_WAIT_EVENT,
    CONTROL_DEFERRED_SAVE_STATE,
    CONTROL_DEFERRED_LOAD_STATE,
    CONTROL_DEFERRED_HISTORY_STATUS,
    CONTROL_DEFERRED_HISTORY_DATA
} control_deferred_kind;

typedef struct deferred_control_response {
    bool active;
    uint32_t request_id;
    control_deferred_kind kind;
    uint64_t deadline_ms;
    uint64_t request_token;
    uint64_t connection_epoch;
    uint16_t memory_address;
    uint32_t memory_length;
    uint8_t memory_mode;
    uint32_t wait_frame_delta;
    uint64_t wait_frame_start;
    char event_name[48];
} deferred_control_response;

typedef struct deferred_control_table {
    deferred_control_response entries[CONTROL_DEFERRED_CAPACITY];
} deferred_control_table;

void control_deferred_clear(deferred_control_response *d);

/* Reserve the single slot. NULL if busy. */
deferred_control_response *control_deferred_reserve(
    deferred_control_table *table,
    const char **out_busy_msg);

deferred_control_response *control_deferred_active(deferred_control_table *table);

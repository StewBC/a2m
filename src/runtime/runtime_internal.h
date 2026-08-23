#pragma once

#include "apple2.h"
#include "audio_buffer.h"
#include "display_frame.h"
#include "keyboard.h"
#include "memview.h"
#include "mutex.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_command.h"
#include "runtime_event.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_timemachine.h"
#include "symbol_table.h"
#include "apple_type_script.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct message_queue message_queue;
typedef struct thread thread;

bool runtime_quit_requested(const runtime *rt);

bool runtime_tm_bp_add(
    runtime *rt,
    const runtime_breakpoint_definition *definition,
    uint32_t *out_id);
bool runtime_tm_bp_update(
    runtime *rt,
    uint32_t id,
    const runtime_breakpoint_definition *definition);
bool runtime_tm_bp_clear(runtime *rt, uint32_t id);
void runtime_tm_bp_clear_all(runtime *rt);
bool runtime_tm_bp_set_enabled(runtime *rt, uint32_t id, bool enabled);
void runtime_tm_bp_toggle_execute(runtime *rt, uint16_t address);
void runtime_tm_bp_fill_snapshot(const runtime *rt, runtime_breakpoint_snapshot *out);
size_t runtime_tm_bp_count(const runtime *rt);

enum {
    RUNTIME_COMMAND_QUEUE_CAPACITY = 256,
    RUNTIME_EVENT_QUEUE_CAPACITY = 256,
    RUNTIME_BREAKPOINT_CAPACITY = 64,
    RUNTIME_RUN_BATCH_CYCLES = 1024,
    RUNTIME_MAX_DISKII_MOUNTS = 16,
    RUNTIME_MAX_SMARTPORT_MOUNTS = 16
};

typedef enum runtime_exec_state {
    RUNTIME_EXEC_STOPPED = 0,
    RUNTIME_EXEC_PAUSED,
    RUNTIME_EXEC_RUNNING
} runtime_exec_state;

typedef struct runtime_frame_slot {
    mutex *mutex;
    /* Latest ARGB frame for UI (Apple size). Also mirrored as display_frame meta. */
    uint32_t *argb;
    uint32_t width;
    uint32_t height;
    uint64_t frame_number;
    bool has_frame;
    uint64_t published_frames;
    uint64_t consumed_frames;
    uint64_t dropped_frames;
} runtime_frame_slot;

typedef struct runtime_debug_memory_slot {
    mutex *mutex;
    runtime_debug_memory_snapshot snapshot;
    bool has_snapshot;
    uint64_t generation;
} runtime_debug_memory_slot;

typedef struct runtime_breakpoint_slot {
    mutex *mutex;
    runtime_breakpoint_snapshot snapshot;
    bool has_snapshot;
} runtime_breakpoint_slot;

typedef struct runtime_symbol_slot {
    mutex *mutex;
    runtime_symbol_snapshot snapshot;
    bool has_symbols;
} runtime_symbol_slot;

typedef enum runtime_rpc_payload_kind {
    RUNTIME_RPC_PAYLOAD_NONE = 0,
    RUNTIME_RPC_PAYLOAD_MEMORY,
    RUNTIME_RPC_PAYLOAD_HISTORY
} runtime_rpc_payload_kind;

typedef struct runtime_rpc_payload_slot {
    uint64_t request_token;
    uint32_t length;
    runtime_rpc_payload_kind kind;
    union {
        struct {
            uint16_t address;
            runtime_memory_mode mode;
        } memory;
        runtime_history_rpc_meta history;
    } meta;
    uint8_t in_use;
    uint8_t *bytes;
} runtime_rpc_payload_slot;

typedef struct runtime_rpc_payload_pool {
    mutex *mutex;
    runtime_rpc_payload_slot slots[RUNTIME_RPC_PAYLOAD_POOL_CAPACITY];
} runtime_rpc_payload_pool;

typedef struct runtime_history_cursor {
    runtime_history_query query;
    uint64_t id;
    uint64_t epoch;
    uint64_t mutation_generation;
    uint64_t next_id;
    uint8_t active;
    uint8_t stale;
} runtime_history_cursor;

enum {
    RUNTIME_SESSION_CAPACITY = 4
};

/* Per-asker state: history cursor + endpoint binding. No live machine pointers. */
typedef struct runtime_session {
    uint32_t id; /* never 0 when active */
    runtime_session_kind kind;
    uint8_t active;
    uint64_t endpoint_epoch; /* control connection_epoch; 0 if unused */
    runtime_history_cursor history_cursor;
} runtime_session;

struct runtime_client {
    message_queue *command_queue;
    message_queue *event_queue;
    runtime_frame_slot *frame_slot;
    runtime_debug_memory_slot *debug_memory_slot;
    runtime_symbol_slot *symbol_slot;
    runtime_breakpoint_slot *breakpoint_slot;
    runtime_rpc_payload_pool *rpc_payload_pool;
    runtime_frame_ring *frame_ring;
    uint64_t next_request_token;
    /* Stamped onto outgoing commands as source session (0 = unknown). */
    uint32_t command_session_id;
};

typedef struct runtime_breakpoint {
    uint32_t id;
    bool enabled;
    uint16_t start_address;
    uint16_t end_address;
    bool has_end_address;
    uint32_t access_mask;
    runtime_breakpoint_mapping mapping;
    uint32_t action_mask;
    bool use_counter;
    uint32_t initial_count;
    uint32_t reset_count;
    uint32_t counter;
    uint32_t current_hits;
    uint8_t swap_slot;
    int32_t swap_param;
    uint8_t swap_relative;
    char tron_path[RUNTIME_BREAKPOINT_TRON_PATH_MAX];
    char type_text[RUNTIME_BREAKPOINT_TYPE_TEXT_MAX];
    runtime_bp_condition condition;
} runtime_breakpoint;

struct runtime {
    thread *thread;
    atomic_bool quit_requested;
    message_queue *command_queue;
    message_queue *event_queue;
    runtime_client client;
    runtime_frame_slot frame_slot;
    runtime_debug_memory_slot debug_memory_slot;
    runtime_breakpoint_slot breakpoint_slot;
    runtime_rpc_payload_pool rpc_payload_pool;
    runtime_symbol_slot symbol_slot;
    symbol_table *symbols;

    apple2_t machine;
    host_keyboard host_keyboard;
    bool machine_ready;

    runtime_config config;
    char *diskii_paths[RUNTIME_MAX_DISKII_MOUNTS];
    int diskii_mount_count;
    int diskii_slots[RUNTIME_MAX_DISKII_MOUNTS];
    int diskii_drives[RUNTIME_MAX_DISKII_MOUNTS];
    char *smartport_paths[RUNTIME_MAX_SMARTPORT_MOUNTS];
    int smartport_mount_count;
    int smartport_slots[RUNTIME_MAX_SMARTPORT_MOUNTS];
    int smartport_units[RUNTIME_MAX_SMARTPORT_MOUNTS];

    runtime_frame_ring frame_ring;
    uint32_t frame_ring_memory_mb;

    runtime_history *history;
    uint32_t history_memory_mb;
    uint64_t history_mutation_generation;
    uint64_t next_history_cursor_id;
    runtime_session sessions[RUNTIME_SESSION_CAPACITY];
    uint32_t next_session_id;
    uint32_t default_session_id; /* omit session_id=0 resolves here; never 0 after create */
    /* When true, stop recording while turbo is max; restore on leave max. */
    bool history_off_on_max;
    /* True if we stopped history solely for max (so we may resume on leave). */
    bool history_paused_for_max;

    /* TimeMachine master enable. Off→on arms HST1 + frame ring once. */
    bool timemachine_enabled;
    uint32_t timemachine_memory_mb;
    runtime_tm_focus tm_focus;
    struct runtime_tm_recorder *tm_recorder;
    /* TM3: forensic mode replaces live apple2_t with THEN; NOW blob for exit. */
    bool tm_forensic;
    uint8_t *tm_now_blob;
    size_t tm_now_size;

    /* TRON/TROFF instruction log (C5b) — file open while trace_enabled. */
    bool trace_enabled;
    FILE *trace_file;

    /* Ladder + active: milli-MHz values; 0 = max. See runtime.h. */
    uint32_t turbo_speeds[16];
    uint8_t turbo_speed_count;
    uint32_t active_turbo_multiplier;

    runtime_exec_state exec_state;
    runtime_stop_reason last_stop_reason;
    uint64_t runtime_seq;

    runtime_breakpoint breakpoints[RUNTIME_BREAKPOINT_CAPACITY];
    size_t breakpoint_count;
    uint32_t next_breakpoint_id;
    /* TM5: forensic-only store. Never copied into breakpoints[]. */
    runtime_breakpoint tm_breakpoints[RUNTIME_BREAKPOINT_CAPACITY];
    size_t tm_breakpoint_count;
    uint32_t tm_next_breakpoint_id;
    bool suppress_execute_bp;
    bool temp_bp_active;
    uint16_t temp_bp_address;
    bool temp_bp_skip_current;
    /* R/W match during a cycle; pause after step (c64m hit-pending pattern). */
    bool breakpoint_hit_pending;
    /* Cached: any enabled BP has READ or WRITE access (bus callback fast path). */
    bool has_rw_breakpoints;

    bool pace_initialized;
    uint64_t frame_counter_step;
    uint64_t next_frame_counter;

    /* Max turbo: ~60 Hz wall block paint into live frame slot + ring. */
    bool block_paint_initialized;
    uint64_t next_block_paint_counter;

    audio_buffer *audio_out;
    int audio_sample_rate;
    double audio_cycle_accum;
    /* Host PCM reconstruction state (stereo). Ring buffer holds interleaved
       L,R floats. DC block + Mockingboard post LPF live here, not in the chip. */
    float audio_dc_x_prev[2];
    float audio_dc_y_prev[2];
    float audio_mb_lpf1[2];
    float audio_mb_lpf2[2];

    /* BP TYPE script playback (clipboard paste stays plain text only).
       Paste / TYPE do not change turbo (zip policy). */
    bool type_script_active;
    bool type_script_await_paste;
    size_t type_script_index;
    size_t type_script_count;
    uint16_t type_script_wait_left;
    uint32_t type_script_wait_cycle_accum;
    apple_type_event type_script_events[APPLE_TYPE_EVENTS_MAX];

    bool started;
    bool alive;

    /* Breakpoint INI helpers (c64m). */
    bool use_ini;
    bool save_ini;
    char *ini_path;
};

int runtime_thread_main(void *userdata);
void runtime_rpc_pool_release_token(runtime_rpc_payload_pool *pool, uint64_t token);

/* Map c64m memory mode enum to VIEW_FLAGS for Apple. */
view_flags_t runtime_mode_to_view_flags(runtime_memory_mode mode);

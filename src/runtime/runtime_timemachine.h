#pragma once

/*
 * TimeMachine: runtime-owned forensic engine.
 *
 * TM0: master enable + recorder arming (HST1 + frame ring).
 * TM1: HST1 tape queries (step / over / out / run-to / seek). No apple2 mutate.
 * Checkpoint ring and materialize land in later phases.
 */

#include "apple2.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct runtime runtime;

enum { RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES = 20000u };

enum { RUNTIME_TM_OPCODE_JSR = 0x20 };

typedef struct runtime_tm_focus {
    bool valid;
    runtime_history_record_kind kind;
    uint8_t opcode;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint16_t pc;
    uint32_t timeline;
    uint64_t epoch;
    uint64_t history_id;
    uint64_t cycle;
} runtime_tm_focus;

typedef struct runtime_tm_window {
    bool valid;
    uint64_t epoch;
    uint64_t oldest_id;
    uint64_t newest_id;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
    uint64_t checkpoint_count;
    uint64_t checkpoints_dropped;
    uint64_t media_truncations;
    runtime_history_media_change_kind start_kind;
    uint32_t start_arg1;
} runtime_tm_window;

typedef enum runtime_tm_query_op {
    RUNTIME_TM_QUERY_SEEK_ID = 0,
    RUNTIME_TM_QUERY_SEEK_CYCLE,
    RUNTIME_TM_QUERY_STEP,
    RUNTIME_TM_QUERY_STEP_OVER,
    RUNTIME_TM_QUERY_STEP_OUT,
    RUNTIME_TM_QUERY_RUN_TO_PC
} runtime_tm_query_op;

typedef enum runtime_tm_query_status {
    RUNTIME_TM_QUERY_OK = 0,
    RUNTIME_TM_QUERY_UNAVAILABLE,
    RUNTIME_TM_QUERY_EMPTY,
    RUNTIME_TM_QUERY_END_OF_TAPE,
    RUNTIME_TM_QUERY_EPOCH_MISMATCH,
    RUNTIME_TM_QUERY_NOT_RETAINED,
    RUNTIME_TM_QUERY_SP_WRAP,
    RUNTIME_TM_QUERY_INVALID
} runtime_tm_query_status;

typedef struct runtime_tm_query_args {
    int direction; /* STEP: +1 forward, -1 backward */
    uint16_t target_pc;
    uint64_t cycle_ceiling; /* RUN_TO_PC: 0 = none */
    uint64_t history_id;
    uint64_t cycle;
    uint64_t epoch; /* SEEK_ID: 0 = current window epoch */
} runtime_tm_query_args;

typedef struct runtime_tm_query_result {
    runtime_tm_query_status status;
    runtime_tm_focus focus;
    bool clamped;
} runtime_tm_query_result;

void runtime_tm_set_enabled(runtime *rt, bool enabled);
bool runtime_tm_enabled(const runtime *rt);
uint32_t runtime_tm_memory_mb(const runtime *rt);

void runtime_tm_window_info(const runtime *rt, runtime_tm_window *out);
void runtime_tm_get_focus(const runtime *rt, runtime_tm_focus *out);

runtime_tm_query_status runtime_tm_query(
    runtime *rt,
    runtime_tm_query_op op,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *out_result);

void runtime_tm_recorder_set_enabled(runtime *rt, bool enabled);
bool runtime_tm_checkpoint_take(runtime *rt);
bool runtime_tm_materialize(runtime *rt, uint64_t cycle, apple2_t *dst);
void runtime_tm_after_step(runtime *rt);
void runtime_tm_on_history_resume(runtime *rt);
void runtime_tm_on_history_invalidate(runtime *rt);
void runtime_tm_on_media_event(
    runtime *rt, uint64_t cycle, int slot, int device, int kind);

uint64_t runtime_tm_checkpoint_count(const runtime *rt);
uint64_t runtime_tm_checkpoints_dropped(const runtime *rt);
uint64_t runtime_tm_media_truncations(const runtime *rt);
void runtime_tm_recorder_destroy(runtime *rt);
void runtime_tm_checkpoint_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *newest, uint64_t *count);
void runtime_tm_fill_window_extras(const runtime *rt, runtime_tm_window *out);

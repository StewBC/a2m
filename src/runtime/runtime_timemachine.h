#pragma once

/*
 * TimeMachine: runtime-owned recording + time travel.
 *
 * TM0: master enable + recorder arming (HST1 + frame ring).
 * TM1 tape-nav: removed in TMA2. HST1 FIND stays in runtime_history.
 * TM2: checkpoint ring + sealed materialize to scratch.
 * TM3: time-travel mode (wire name: forensic) materializes into the live
 *      apple2_t; exit restores NOW.
 */

#include "apple2.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct runtime runtime;

enum { RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES = 20000u };

/* Landed Apple (machine cycles / regs). Not an HST1 tape head. */
typedef struct runtime_tm_focus {
    bool valid;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint16_t pc;
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

typedef enum runtime_tm_mode {
    RUNTIME_TM_MODE_LIVE = 0,
    RUNTIME_TM_MODE_FORENSIC
} runtime_tm_mode;

typedef enum runtime_tm_enter_status {
    RUNTIME_TM_ENTER_OK = 0,
    RUNTIME_TM_ENTER_UNAVAILABLE,
    RUNTIME_TM_ENTER_EMPTY,
    RUNTIME_TM_ENTER_FAILED
} runtime_tm_enter_status;

void runtime_tm_set_enabled(runtime *rt, bool enabled);
bool runtime_tm_enabled(const runtime *rt);
uint32_t runtime_tm_memory_mb(const runtime *rt);

void runtime_tm_window_info(const runtime *rt, runtime_tm_window *out);
void runtime_tm_get_focus(const runtime *rt, runtime_tm_focus *out);

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
bool runtime_tm_recorder_is_recording(const runtime *rt);
void runtime_tm_recorder_destroy(runtime *rt);
void runtime_tm_checkpoint_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *newest, uint64_t *count);
void runtime_tm_fill_window_extras(const runtime *rt, runtime_tm_window *out);
void runtime_tm_timeline_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *live, uint64_t *count);
uint64_t runtime_tm_live_cycle(const runtime *rt);
bool runtime_tm_at_live(const runtime *rt);
void runtime_tm_sync_focus(runtime *rt);
void runtime_tm_apply_logged_inputs(
    runtime *rt, apple2_t *dst, uint64_t from_inclusive, uint64_t to_inclusive);
bool runtime_tm_load_nearest_checkpoint(runtime *rt, uint64_t cycle);
bool runtime_tm_restore_live(runtime *rt);
bool runtime_tm_land(runtime *rt, uint64_t cycle);
bool runtime_tm_reexecute_to(runtime *rt, uint64_t target_cycle);
bool runtime_tm_frame_step(runtime *rt, int direction);

runtime_tm_mode runtime_tm_current_mode(const runtime *rt);
bool runtime_tm_in_forensic(const runtime *rt);
runtime_tm_enter_status runtime_tm_can_enter(const runtime *rt);
runtime_tm_enter_status runtime_tm_enter_forensic(runtime *rt);
void runtime_tm_exit_forensic(runtime *rt);
bool runtime_tm_materialize_live(runtime *rt, uint64_t cycle);
bool runtime_tm_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size);
bool runtime_tm_restore_blob(runtime *rt, const uint8_t *blob, size_t size);
void runtime_tm_forensic_destroy(runtime *rt);
const char *runtime_tm_window_start_name(runtime_history_media_change_kind kind);

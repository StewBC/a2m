#pragma once

/*
 * Inspector: runtime-owned recording + time travel.
 *
 * TM0: master enable + recorder arming (HST1 + frame ring).
 * TM1 tape-nav: removed in TMA2. HST1 FIND stays in runtime_history.
 * TM2: frame-aligned sample ring + sealed materialize to scratch.
 * TM3: Inspect mode (wire name: inspector) materializes into the live
 *      apple2_t; leave restores NOW.
 */

#include "apple2.h"
#include "runtime_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct runtime runtime;

typedef enum runtime_inspector_sample_kind {
    RUNTIME_INSPECTOR_SAMPLE_FINITE_FRAME = 1,
    RUNTIME_INSPECTOR_SAMPLE_MAX_FRAME,
    RUNTIME_INSPECTOR_SAMPLE_NOW
} runtime_inspector_sample_kind;

typedef enum runtime_inspector_execution_mode {
    RUNTIME_INSPECTOR_EXECUTION_FINITE = 0,
    RUNTIME_INSPECTOR_EXECUTION_MAX = 1
} runtime_inspector_execution_mode;

typedef struct runtime_inspector_sample_meta {
    uint64_t sample_id;
    uint64_t timeline_generation;
    uint64_t frame_cycle;
    uint64_t snapshot_cycle;
    uint64_t frame_number;
    uint64_t picture_id;
    uint64_t frame_replay_watermark;
    uint64_t snapshot_replay_watermark;
    uint8_t execution_mode;
    uint8_t kind;
    uint8_t picture_available;
} runtime_inspector_sample_meta;

typedef struct runtime_inspector_catalog {
    uint64_t timeline_generation;
    uint64_t count;
    size_t capacity;
    runtime_inspector_sample_meta *samples;
} runtime_inspector_catalog;

void runtime_inspector_catalog_destroy(runtime_inspector_catalog *catalog);

/* Landed Apple (machine cycles / regs). Not an HST1 tape head. */
typedef struct runtime_inspector_focus {
    bool valid;
    bool is_sample;
    uint8_t sp;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t p;
    uint16_t pc;
    uint64_t cycle;
    uint64_t sample_id;
    uint64_t catalog_ordinal;
} runtime_inspector_focus;

typedef struct runtime_inspector_window {
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
} runtime_inspector_window;

typedef enum runtime_inspector_mode {
    RUNTIME_INSPECTOR_MODE_LIVE = 0,
    RUNTIME_INSPECTOR_MODE_INSPECT
} runtime_inspector_mode;

typedef enum runtime_inspector_enter_status {
    RUNTIME_INSPECTOR_ENTER_OK = 0,
    RUNTIME_INSPECTOR_ENTER_UNAVAILABLE,
    RUNTIME_INSPECTOR_ENTER_EMPTY,
    RUNTIME_INSPECTOR_ENTER_FAILED
} runtime_inspector_enter_status;

void runtime_inspector_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_enabled(const runtime *rt);
uint32_t runtime_inspector_memory_mb(const runtime *rt);

void runtime_inspector_window_info(const runtime *rt, runtime_inspector_window *out);
void runtime_inspector_get_focus(const runtime *rt, runtime_inspector_focus *out);

void runtime_inspector_recorder_set_enabled(runtime *rt, bool enabled);
bool runtime_inspector_checkpoint_take(runtime *rt);
bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, apple2_t *dst);
void runtime_inspector_after_step(runtime *rt);
uint64_t runtime_inspector_on_finite_cadence_frame(
    runtime *rt,
    uint64_t frame_cycle,
    uint64_t frame_number,
    const uint32_t *pixels);
uint64_t runtime_inspector_on_max_cadence_frame(
    runtime *rt,
    uint64_t snapshot_cycle,
    uint64_t frame_number,
    const uint32_t *pixels);
void runtime_inspector_on_instruction_boundary(runtime *rt);
void runtime_inspector_on_execution_mode_transition(
    runtime *rt,
    bool entering_max,
    bool leaving_max);
void runtime_inspector_mark_live_advanced(runtime *rt);
void runtime_inspector_mark_live_mutated(runtime *rt);
void runtime_inspector_mark_presentation_changed(runtime *rt);
uint64_t runtime_inspector_timeline_generation(const runtime *rt);
uint64_t runtime_inspector_sample_count(const runtime *rt);
bool runtime_inspector_sample_meta_at(
    const runtime *rt,
    uint64_t ordinal,
    runtime_inspector_sample_meta *out);
bool runtime_inspector_sample_meta_by_id(
    const runtime *rt,
    uint64_t sample_id,
    runtime_inspector_sample_meta *out,
    uint64_t *ordinal);
bool runtime_inspector_reconstruct_sample_picture(
    runtime *rt,
    uint64_t sample_id,
    uint32_t *out_pixels,
    size_t out_pixel_count);
void runtime_inspector_publish_catalog(runtime *rt);
void runtime_inspector_on_history_resume(runtime *rt);
void runtime_inspector_on_history_invalidate(runtime *rt);
void runtime_inspector_on_media_event(
    runtime *rt, uint64_t cycle, int slot, int device, int kind);

uint64_t runtime_inspector_checkpoint_count(const runtime *rt);
uint64_t runtime_inspector_checkpoints_dropped(const runtime *rt);
uint64_t runtime_inspector_media_truncations(const runtime *rt);
bool runtime_inspector_recorder_is_recording(const runtime *rt);
void runtime_inspector_recorder_destroy(runtime *rt);
void runtime_inspector_checkpoint_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *newest, uint64_t *count);
void runtime_inspector_fill_window_extras(const runtime *rt, runtime_inspector_window *out);
void runtime_inspector_timeline_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *live, uint64_t *count);
uint64_t runtime_inspector_live_cycle(const runtime *rt);
bool runtime_inspector_at_live(const runtime *rt);
void runtime_inspector_sync_focus(runtime *rt);
void runtime_inspector_apply_logged_inputs(
    runtime *rt, apple2_t *dst, uint64_t from_inclusive, uint64_t to_inclusive);
bool runtime_inspector_load_nearest_checkpoint(runtime *rt, uint64_t cycle);
bool runtime_inspector_restore_live(runtime *rt);
bool runtime_inspector_land(runtime *rt, uint64_t cycle);
bool runtime_inspector_land_sample(runtime *rt, uint64_t sample_id);
/* Exact land: nearest checkpoint ≤ target then sealed reexecute_to(target)
   in one call (no intermediate UI publish). Returns false on hard failure or
   if focus could not reach the clamped target (best-effort focus still set). */
bool runtime_inspector_land_to_cycle(runtime *rt, uint64_t target_cycle);
bool runtime_inspector_reexecute_to(runtime *rt, uint64_t target_cycle);
bool runtime_inspector_step_sample(runtime *rt, int direction);

runtime_inspector_mode runtime_inspector_current_mode(const runtime *rt);
bool runtime_inspector_inspecting(const runtime *rt);
runtime_inspector_enter_status runtime_inspector_can_enter(const runtime *rt);
runtime_inspector_enter_status runtime_inspector_enter(runtime *rt);
void runtime_inspector_leave(runtime *rt);
bool runtime_inspector_materialize_live(runtime *rt, uint64_t cycle);
bool runtime_inspector_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size);
bool runtime_inspector_restore_blob(runtime *rt, const uint8_t *blob, size_t size);
void runtime_inspector_destroy(runtime *rt);
const char *runtime_inspector_window_start_name(runtime_history_media_change_kind kind);

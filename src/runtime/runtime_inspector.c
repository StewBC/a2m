#include "runtime_inspector.h"

#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "video.h"

#include "a2m_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runtime_inspector_warn_zero_budget(const runtime *rt)
{
    char msg[256];
    size_t used = 0;
    int history_zero;
    int frame_zero;
    int inspector_zero;
    int need_and = 0;

    if (rt == NULL || !rt->inspector_enabled) {
        return;
    }
    history_zero = (rt->history_memory_mb == 0u);
    frame_zero = (rt->frame_ring_memory_mb == 0u);
    inspector_zero = (rt->inspector_memory_mb == 0u);
    if (!history_zero && !frame_zero && !inspector_zero) {
        return;
    }

    used = (size_t)snprintf(msg, sizeof(msg), "inspector=1 but");
    if (history_zero && used < sizeof(msg)) {
        used += (size_t)snprintf(msg + used, sizeof(msg) - used, " history_memory_mb=0");
        need_and = 1;
    }
    if (frame_zero && used < sizeof(msg)) {
        used += (size_t)snprintf(
            msg + used,
            sizeof(msg) - used,
            "%s frame_ring_memory_mb=0",
            need_and ? " and" : "");
        need_and = 1;
    }
    if (inspector_zero && used < sizeof(msg)) {
        used += (size_t)snprintf(
            msg + used,
            sizeof(msg) - used,
            "%s inspector_memory_mb=0",
            need_and ? " and" : "");
    }
    if (used < sizeof(msg)) {
        (void)snprintf(msg + used, sizeof(msg) - used, "; Inspector window will be empty");
    }
    log_warn("%s", msg);
}

void runtime_inspector_set_enabled(runtime *rt, bool enabled)
{
    bool was_enabled;

    if (rt == NULL) {
        return;
    }

    if (!enabled) {
        rt->inspector_enabled = false;
        runtime_inspector_recorder_set_enabled(rt, false);
        runtime_inspector_on_history_invalidate(rt);
        return;
    }

    was_enabled = rt->inspector_enabled;
    rt->inspector_enabled = true;
    if (was_enabled) {
        return;
    }

    if (rt->history != NULL) {
        uint64_t cycle = rt->machine_ready ? apple2_cycles(&rt->machine) : 0u;
        (void)runtime_history_resume(rt->history, cycle);
    }
    if (rt->frame_ring_memory_mb > 0u) {
        runtime_frame_ring_set_recording(&rt->frame_ring, true);
    }
    runtime_inspector_recorder_set_enabled(rt, true);
    runtime_inspector_warn_zero_budget(rt);
}

bool runtime_inspector_enabled(const runtime *rt)
{
    return rt != NULL && rt->inspector_enabled;
}

uint32_t runtime_inspector_memory_mb(const runtime *rt)
{
    return rt != NULL ? rt->inspector_memory_mb : 0u;
}

void runtime_inspector_window_info(const runtime *rt, runtime_inspector_window *out)
{
    runtime_inspector_sample_meta first;
    runtime_inspector_sample_meta last;
    uint64_t count;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (rt == NULL) {
        return;
    }
    count = runtime_inspector_sample_count(rt);
    if (count == 0u ||
        !runtime_inspector_sample_meta_at(rt, 0u, &first) ||
        !runtime_inspector_sample_meta_at(rt, count - 1u, &last)) {
        return;
    }
    out->valid = true;
    out->epoch = first.timeline_generation;
    out->oldest_id = first.sample_id;
    out->newest_id = last.sample_id;
    out->oldest_cycle = first.snapshot_cycle;
    out->newest_cycle = last.snapshot_cycle;
    runtime_inspector_fill_window_extras(rt, out);
}

void runtime_inspector_get_focus(const runtime *rt, runtime_inspector_focus *out)
{
    if (out == NULL) {
        return;
    }
    if (rt == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = rt->inspector_focus;
}

runtime_inspector_mode runtime_inspector_current_mode(const runtime *rt)
{
    return (rt != NULL && rt->inspecting) ?
        RUNTIME_INSPECTOR_MODE_INSPECT : RUNTIME_INSPECTOR_MODE_LIVE;
}

bool runtime_inspector_inspecting(const runtime *rt)
{
    return rt != NULL && rt->inspecting;
}

const char *runtime_inspector_window_start_name(runtime_history_media_change_kind kind)
{
    switch (kind) {
    case RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE:
        return "guest-write";
    case RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY:
        return "host-directory";
    case RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN:
    default:
        return "unknown";
    }
}

bool runtime_inspector_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size)
{
    size_t need;
    uint8_t *bytes;
    size_t written;

    if (rt == NULL || blob == NULL || size == NULL || !rt->machine_ready) {
        return false;
    }
    need = apple2_snapshot_size(&rt->machine);
    if (need == 0u) {
        return false;
    }
    bytes = (uint8_t *)malloc(need);
    if (bytes == NULL) {
        return false;
    }
    written = apple2_snapshot_save(&rt->machine, bytes, need);
    if (written != need) {
        free(bytes);
        return false;
    }
    *blob = bytes;
    *size = written;
    return true;
}

bool runtime_inspector_restore_blob(runtime *rt, const uint8_t *blob, size_t size)
{
    if (rt == NULL || blob == NULL || size == 0u || !rt->machine_ready) {
        return false;
    }
    if (!apple2_snapshot_load(&rt->machine, blob, size)) {
        return false;
    }
    apple2_video_reseed_from_cycles(&rt->machine);
    return true;
}

static void inspector_clear_now_picture(runtime *rt)
{
    runtime_inspector_picture_slot *slot;
    if (rt == NULL) return;
    slot = &rt->inspector_picture_slot;
    if (slot->mutex == NULL) return;
    mutex_lock(slot->mutex);
    slot->valid = false;
    slot->picture_id = 0u;
    slot->frame_number = 0u;
    mutex_unlock(slot->mutex);
}

static bool inspector_store_now_picture(
    runtime *rt, uint64_t picture_id, uint64_t frame_number,
    const uint32_t *pixels)
{
    runtime_inspector_picture_slot *slot;
    uint32_t *allocated = NULL;
    const size_t bytes =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT * sizeof(uint32_t);
    if (rt == NULL || picture_id == 0u || pixels == NULL) return false;
    slot = &rt->inspector_picture_slot;
    if (slot->mutex == NULL) return false;
    if (slot->argb == NULL) {
        allocated = (uint32_t *)malloc(bytes);
        if (allocated == NULL) return false;
    }
    mutex_lock(slot->mutex);
    if (slot->argb == NULL) {
        slot->argb = allocated;
        allocated = NULL;
    }
    memcpy(slot->argb, pixels, bytes);
    slot->picture_id = picture_id;
    slot->frame_number = frame_number;
    slot->valid = true;
    mutex_unlock(slot->mutex);
    free(allocated);
    return true;
}

bool runtime_inspector_now_picture_available(runtime *rt, uint64_t picture_id)
{
    runtime_inspector_picture_slot *slot;
    bool available;
    if (rt == NULL || picture_id == 0u) return false;
    slot = &rt->inspector_picture_slot;
    if (slot->mutex == NULL) return false;
    mutex_lock(slot->mutex);
    available = slot->valid && slot->picture_id == picture_id;
    mutex_unlock(slot->mutex);
    return available;
}

static void inspector_discard_now(runtime *rt)
{
    if (rt == NULL) return;
    inspector_clear_now_picture(rt);
    free(rt->inspector_now_blob);
    free(rt->inspector_now_resume_framebuffer);
    rt->inspector_now_blob = NULL;
    rt->inspector_now_resume_framebuffer = NULL;
    rt->inspector_now_size = 0u;
    rt->inspector_now_cycle = 0u;
    rt->inspector_now_endpoint_id = 0u;
    rt->inspector_now_aliased_sample_id = 0u;
    rt->inspector_now_valid = false;
    rt->inspector_now_aliases_sample = false;
}

void runtime_inspector_mark_live_advanced(runtime *rt)
{
    if (rt == NULL || rt->inspecting) return;
    rt->apple_state_generation++;
    if (rt->apple_state_generation == 0u) rt->apple_state_generation = 1u;
    if (rt->inspector_now_valid) inspector_discard_now(rt);
}

void runtime_inspector_mark_live_mutated(runtime *rt)
{
    runtime_inspector_mark_live_advanced(rt);
}

void runtime_inspector_mark_presentation_changed(runtime *rt)
{
    if (rt == NULL) return;
    rt->presentation_generation++;
    if (rt->presentation_generation == 0u) rt->presentation_generation = 1u;
    rt->inspector_has_presentation = false;
    inspector_clear_now_picture(rt);
}

void runtime_inspector_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    inspector_discard_now(rt);
    rt->inspecting = false;
}

uint64_t runtime_inspector_live_cycle(const runtime *rt)
{
    uint64_t oldest = 0u;
    uint64_t newest_cp = 0u;
    uint64_t count = 0u;
    uint64_t live;

    if (rt == NULL) {
        return 0u;
    }
    if (rt->inspecting && rt->inspector_now_blob != NULL) {
        live = rt->inspector_now_cycle;
    } else if (rt->machine_ready) {
        live = apple2_cycles(&rt->machine);
    } else {
        live = 0u;
    }
    runtime_inspector_checkpoint_bounds(rt, &oldest, &newest_cp, &count);
    if (count > 0u && newest_cp > live) {
        live = newest_cp;
    }
    return live;
}

void runtime_inspector_timeline_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *live, uint64_t *count)
{
    uint64_t cp_old = 0u;
    uint64_t cp_new = 0u;
    uint64_t n = 0u;

    if (oldest != NULL) {
        *oldest = 0u;
    }
    if (live != NULL) {
        *live = 0u;
    }
    if (count != NULL) {
        *count = 0u;
    }
    runtime_inspector_checkpoint_bounds(rt, &cp_old, &cp_new, &n);
    if (n == 0u) {
        return;
    }
    if (oldest != NULL) {
        *oldest = cp_old;
    }
    if (live != NULL) {
        *live = runtime_inspector_live_cycle(rt);
    }
    if (count != NULL) {
        *count = n;
    }
}

bool runtime_inspector_at_live(const runtime *rt)
{
    uint64_t live;

    if (rt == NULL || !rt->inspecting || !rt->machine_ready) {
        return false;
    }
    live = rt->inspector_now_cycle;
    if (live == 0u) {
        live = runtime_inspector_live_cycle(rt);
    }
    return apple2_cycles(&rt->machine) >= live;
}

void runtime_inspector_sync_focus(runtime *rt)
{
    if (rt == NULL || !rt->machine_ready) {
        return;
    }
    /* A sealed CPU/cycle focus has no paired historical picture.  Callers
       which select a catalog sample repopulate presentation_scratch after
       synchronizing the machine focus. */
    rt->inspector_has_presentation = false;
    memset(&rt->inspector_focus, 0, sizeof(rt->inspector_focus));
    rt->inspector_focus.valid = true;
    rt->inspector_focus.cycle = apple2_cycles(&rt->machine);
    rt->inspector_focus.pc = rt->machine.cpu.cpu.pc;
    rt->inspector_focus.a = rt->machine.cpu.cpu.A;
    rt->inspector_focus.x = rt->machine.cpu.cpu.X;
    rt->inspector_focus.y = rt->machine.cpu.cpu.Y;
    rt->inspector_focus.p = rt->machine.cpu.cpu.flags;
    rt->inspector_focus.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xffu);
}

static bool inspector_ensure_presentation(runtime *rt)
{
    const size_t pixels =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;
    if (rt->presentation_scratch == NULL) {
        rt->presentation_scratch =
            (uint32_t *)malloc(pixels * sizeof(*rt->presentation_scratch));
    }
    return rt->presentation_scratch != NULL;
}

static void inspector_select_sample_focus(
    runtime *rt,
    const runtime_inspector_sample_meta *meta,
    uint64_t ordinal)
{
    runtime_ring_frame *frame;
    const size_t pixels =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;

    runtime_inspector_sync_focus(rt);
    rt->inspector_focus.is_sample = true;
    rt->inspector_focus.sample_id = meta->sample_id;
    rt->inspector_focus.catalog_ordinal = ordinal;
    rt->inspector_has_presentation = false;
    if (!inspector_ensure_presentation(rt)) {
        return;
    }
    frame = (runtime_ring_frame *)malloc(sizeof(*frame));
    if (frame == NULL) {
        return;
    }
    if (runtime_frame_ring_copy_by_picture_id(
            &rt->frame_ring, meta->picture_id, frame)) {
        memcpy(
            rt->presentation_scratch,
            frame->pixels,
            pixels * sizeof(*rt->presentation_scratch));
        rt->inspector_has_presentation = true;
        free(frame);
        return;
    }
    free(frame);
    if (runtime_inspector_reconstruct_sample_picture(
            rt, meta->sample_id, rt->presentation_scratch, pixels)) {
        rt->inspector_has_presentation = true;
        return;
    }
    /* Honest best-effort fallback.  The target snapshot and its exact resume
       framebuffer remain authoritative even when historical pixels are gone. */
    if (apple2_video_paint_full_frame_to(
            &rt->machine, rt->presentation_scratch, pixels)) {
        rt->inspector_has_presentation = true;
    }
}

static void inspector_prepare_now_presentation(runtime *rt)
{
    runtime_inspector_sample_meta newest;
    runtime_ring_frame *frame;
    uint64_t count;
    uint64_t frame_number = 0u;
    const size_t pixels =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;

    rt->inspector_has_presentation = false;
    if (!inspector_ensure_presentation(rt)) return;
    count = runtime_inspector_sample_count(rt);
    if (count > 0u && runtime_inspector_sample_meta_at(rt, count - 1u, &newest)) {
        frame = (runtime_ring_frame *)malloc(sizeof(*frame));
        if (frame != NULL) {
            if (runtime_frame_ring_copy_by_picture_id(
                    &rt->frame_ring, newest.picture_id, frame)) {
                memcpy(
                    rt->presentation_scratch,
                    frame->pixels,
                    pixels * sizeof(*rt->presentation_scratch));
                rt->inspector_has_presentation = true;
                frame_number = frame->frame_number;
            }
            free(frame);
        }
        if (!rt->inspector_has_presentation &&
            runtime_inspector_reconstruct_sample_picture(
                rt, newest.sample_id, rt->presentation_scratch, pixels)) {
            (void)runtime_inspector_restore_live(rt);
            rt->inspector_has_presentation = true;
        }
    }
    if (!rt->inspector_has_presentation && apple2_video_paint_full_frame_to(
            &rt->machine, rt->presentation_scratch, pixels)) {
        rt->inspector_has_presentation = true;
    }
    if (rt->inspector_has_presentation) {
        (void)inspector_store_now_picture(
            rt, rt->inspector_now_endpoint_id, frame_number,
            rt->presentation_scratch);
    }
}

runtime_inspector_enter_status runtime_inspector_can_enter(const runtime *rt)
{
    if (rt == NULL || !rt->machine_ready) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (rt->inspecting) {
        return RUNTIME_INSPECTOR_ENTER_OK;
    }
    if (!rt->inspector_enabled) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (runtime_inspector_checkpoint_count(rt) == 0u) {
        return RUNTIME_INSPECTOR_ENTER_EMPTY;
    }
    return RUNTIME_INSPECTOR_ENTER_OK;
}

static void inspector_apply_live_seal(runtime *rt)
{
    apple2_set_replay_sealed(&rt->machine, true);
    apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    apple2_set_memory_access_callback(&rt->machine, NULL, NULL);
}

bool runtime_inspector_materialize_live(runtime *rt, uint64_t cycle)
{
    bool ok;

    if (rt == NULL || !rt->machine_ready) {
        return false;
    }
    inspector_apply_live_seal(rt);
    ok = runtime_inspector_materialize(rt, cycle, &rt->machine);
    /* Scratch materialize clears the seal; Inspect stays sealed. */
    inspector_apply_live_seal(rt);
    apple2_video_reseed_from_cycles(&rt->machine);
    return ok;
}

bool runtime_inspector_restore_live(runtime *rt)
{
    if (rt == NULL || rt->inspector_now_blob == NULL || rt->inspector_now_size == 0u) {
        return false;
    }
    if (!runtime_inspector_restore_blob(rt, rt->inspector_now_blob, rt->inspector_now_size)) {
        return false;
    }
    if (rt->inspector_now_resume_framebuffer != NULL) {
        memcpy(
            rt->machine.video.fb,
            rt->inspector_now_resume_framebuffer,
            (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT * sizeof(uint32_t));
    }
    rt->active_turbo_multiplier = rt->inspector_now_live_turbo_value;
    rt->machine.video.paint_enabled =
        rt->inspector_now_execution_mode == RUNTIME_INSPECTOR_EXECUTION_FINITE;
    inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    rt->inspector_has_presentation = false;
    return true;
}

bool runtime_inspector_land_sample(runtime *rt, uint64_t sample_id)
{
    runtime_inspector_sample_meta meta;
    uint64_t ordinal;

    if (rt == NULL || !rt->machine_ready || !rt->inspecting) {
        return false;
    }
    if (rt->inspector_now_valid &&
        sample_id == rt->inspector_now_endpoint_id) {
        if (!runtime_inspector_restore_live(rt)) return false;
        rt->inspector_focus.is_sample = true;
        rt->inspector_focus.sample_id = sample_id;
        rt->inspector_focus.catalog_ordinal = rt->inspector_now_aliases_sample ?
            runtime_inspector_sample_count(rt) - 1u :
            runtime_inspector_sample_count(rt);
        inspector_prepare_now_presentation(rt);
        return true;
    }
    if (
        !runtime_inspector_sample_meta_by_id(
            rt, sample_id, &meta, &ordinal)) {
        return false;
    }
    if (!runtime_inspector_load_nearest_checkpoint(rt, meta.snapshot_cycle) ||
        apple2_cycles(&rt->machine) != meta.snapshot_cycle) {
        return false;
    }
    inspector_apply_live_seal(rt);
    inspector_select_sample_focus(rt, &meta, ordinal);
    return true;
}

bool runtime_inspector_land(runtime *rt, uint64_t cycle)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;

    if (rt == NULL || !rt->machine_ready || !rt->inspecting) {
        return false;
    }
    runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    if (cycle >= live) {
        return runtime_inspector_restore_live(rt);
    }
    if (cycle < oldest) {
        cycle = oldest;
    }
    {
        uint64_t i;
        runtime_inspector_sample_meta meta;
        runtime_inspector_sample_meta best;
        bool found = false;
        for (i = 0u; i < runtime_inspector_sample_count(rt); i++) {
            if (!runtime_inspector_sample_meta_at(rt, i, &meta)) {
                break;
            }
            if (meta.snapshot_cycle <= cycle) {
                best = meta;
                found = true;
            } else {
                break;
            }
        }
        if (!found) {
            return false;
        }
        return runtime_inspector_land_sample(rt, best.sample_id);
    }
}

bool runtime_inspector_land_to_cycle(runtime *rt, uint64_t target_cycle)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;
    uint64_t want;

    if (rt == NULL || !rt->machine_ready || !rt->inspecting) {
        return false;
    }
    runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    if (target_cycle >= live) {
        return runtime_inspector_restore_live(rt);
    }
    want = target_cycle;
    if (want < oldest) {
        want = oldest;
    }
    /* Checkpoint ≤ want, then fill forward — one publish at command end. */
    if (!runtime_inspector_load_nearest_checkpoint(rt, want)) {
        return false;
    }
    if (!runtime_inspector_reexecute_to(rt, want)) {
        return false;
    }
    /* Partial: step failed short of want (focus is best-effort). */
    if (rt->inspector_focus.valid && rt->inspector_focus.cycle != want) {
        return false;
    }
    return true;
}

bool runtime_inspector_reexecute_to(runtime *rt, uint64_t target_cycle)
{
    uint64_t live;

    if (rt == NULL || !rt->machine_ready || !rt->inspecting) {
        return false;
    }
    live = runtime_inspector_live_cycle(rt);
    if (target_cycle > live) {
        target_cycle = live;
    }
    inspector_apply_live_seal(rt);
    while (apple2_cycles(&rt->machine) < target_cycle) {
        uint64_t c0 = apple2_cycles(&rt->machine);
        runtime_inspector_apply_logged_inputs(
            rt, &rt->machine, c0, c0);
        if (rt->machine.video.paint_enabled) {
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
        } else if (apple2_step_instruction_max(&rt->machine) == 0u) {
            break;
        }
    }
    if (apple2_cycles(&rt->machine) >= live) {
        return runtime_inspector_restore_live(rt);
    }
    inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
}

bool runtime_inspector_step_sample(runtime *rt, int direction)
{
    uint64_t permanent_count;
    uint64_t count;
    uint64_t destination;
    runtime_inspector_sample_meta meta;

    if (rt == NULL || !rt->inspecting || direction == 0) {
        return false;
    }
    permanent_count = runtime_inspector_sample_count(rt);
    count = permanent_count +
        ((rt->inspector_now_valid && !rt->inspector_now_aliases_sample) ? 1u : 0u);
    if (count == 0u) {
        return false;
    }
    if (rt->inspector_focus.is_sample) {
        destination = rt->inspector_focus.catalog_ordinal;
        if (direction < 0) {
            if (destination == 0u) return true;
            destination--;
        } else {
            if (destination + 1u >= count) return true;
            destination++;
        }
    } else {
        uint64_t i;
        bool found = false;
        destination = 0u;
        if (direction < 0) {
            for (i = 0u; i < count; i++) {
                if (!runtime_inspector_sample_meta_at(rt, i, &meta)) break;
                if (meta.snapshot_cycle < rt->inspector_focus.cycle) {
                    destination = i;
                    found = true;
                } else break;
            }
        } else {
            for (i = 0u; i < permanent_count; i++) {
                if (!runtime_inspector_sample_meta_at(rt, i, &meta)) break;
                if (meta.snapshot_cycle > rt->inspector_focus.cycle) {
                    destination = i;
                    found = true;
                    break;
                }
            }
            if (!found && rt->inspector_now_valid &&
                !rt->inspector_now_aliases_sample &&
                rt->inspector_now_cycle > rt->inspector_focus.cycle) {
                destination = permanent_count;
                found = true;
            }
        }
        if (!found) return true;
    }
    if (destination == permanent_count && rt->inspector_now_valid &&
        !rt->inspector_now_aliases_sample) {
        return runtime_inspector_land_sample(
            rt, rt->inspector_now_endpoint_id);
    }
    if (!runtime_inspector_sample_meta_at(rt, destination, &meta)) {
        return false;
    }
    return runtime_inspector_land_sample(rt, meta.sample_id);
}

runtime_inspector_enter_status runtime_inspector_enter(runtime *rt)
{
    runtime_inspector_enter_status can;
    uint8_t *now = NULL;
    size_t now_size = 0u;
    uint32_t *resume = NULL;
    runtime_inspector_sample_meta newest;
    uint64_t sample_count;
    bool reuse;

    if (rt == NULL || !rt->machine_ready) {
        return RUNTIME_INSPECTOR_ENTER_UNAVAILABLE;
    }
    if (rt->inspecting) {
        return RUNTIME_INSPECTOR_ENTER_OK;
    }
    can = runtime_inspector_can_enter(rt);
    if (can != RUNTIME_INSPECTOR_ENTER_OK) {
        return can;
    }

    if (runtime_inspector_checkpoint_count(rt) == 0u) {
        return RUNTIME_INSPECTOR_ENTER_EMPTY;
    }
    reuse = rt->inspector_now_valid &&
        rt->inspector_now_machine_generation == rt->apple_state_generation &&
        rt->inspector_now_timeline_generation ==
            runtime_inspector_timeline_generation(rt) &&
        rt->inspector_now_cycle == apple2_cycles(&rt->machine);
    if (!reuse) {
        inspector_discard_now(rt);
        if (!runtime_inspector_snapshot_machine(rt, &now, &now_size)) {
            return RUNTIME_INSPECTOR_ENTER_FAILED;
        }
        resume = (uint32_t *)malloc(
            (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT * sizeof(uint32_t));
        if (resume == NULL) {
            free(now);
            return RUNTIME_INSPECTOR_ENTER_FAILED;
        }
        memcpy(
            resume,
            rt->machine.video.fb,
            (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT * sizeof(uint32_t));
        rt->inspector_now_blob = now;
        rt->inspector_now_size = now_size;
        rt->inspector_now_resume_framebuffer = resume;
        rt->inspector_now_cycle = apple2_cycles(&rt->machine);
        rt->inspector_now_machine_generation = rt->apple_state_generation;
        rt->inspector_now_timeline_generation =
            runtime_inspector_timeline_generation(rt);
        rt->inspector_now_live_turbo_value = rt->active_turbo_multiplier;
        rt->inspector_now_execution_mode =
            runtime_turbo_is_max_value(rt->active_turbo_multiplier) ?
                RUNTIME_INSPECTOR_EXECUTION_MAX : RUNTIME_INSPECTOR_EXECUTION_FINITE;
        rt->inspector_now_endpoint_id = rt->inspector_next_now_endpoint_id++;
        if (rt->inspector_now_endpoint_id == 0u) {
            rt->inspector_now_endpoint_id = rt->inspector_next_now_endpoint_id++;
        }
        rt->inspector_now_valid = true;
        sample_count = runtime_inspector_sample_count(rt);
        rt->inspector_now_aliases_sample = sample_count > 0u &&
            runtime_inspector_sample_meta_at(rt, sample_count - 1u, &newest) &&
            newest.snapshot_cycle == rt->inspector_now_cycle &&
            newest.timeline_generation == rt->inspector_now_timeline_generation;
        if (rt->inspector_now_aliases_sample) {
            rt->inspector_now_aliased_sample_id = newest.sample_id;
            rt->inspector_now_endpoint_id = newest.sample_id;
        }
    }

    /* Stay on NOW (live). Do not SEEK / land an earlier cadence checkpoint. */
    runtime_inspector_recorder_set_enabled(rt, false);
    inspector_apply_live_seal(rt);
    rt->inspecting = true;
    runtime_inspector_sync_focus(rt);
    rt->inspector_focus.is_sample = true;
    rt->inspector_focus.sample_id = rt->inspector_now_endpoint_id;
    rt->inspector_focus.catalog_ordinal = rt->inspector_now_aliases_sample ?
        runtime_inspector_sample_count(rt) - 1u : runtime_inspector_sample_count(rt);
    inspector_prepare_now_presentation(rt);
    runtime_inspector_publish_catalog(rt);
    return RUNTIME_INSPECTOR_ENTER_OK;
}

void runtime_inspector_leave(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    if (!rt->inspecting) {
        return;
    }
    if (rt->inspector_now_blob != NULL && rt->inspector_now_size > 0u) {
        (void)runtime_inspector_restore_live(rt);
    }
    apple2_set_replay_sealed(&rt->machine, false);
    rt->inspecting = false;
    rt->inspector_has_presentation = false;
    if (rt->inspector_enabled) {
        runtime_inspector_recorder_set_enabled(rt, true);
    }
    runtime_inspector_publish_catalog(rt);
}

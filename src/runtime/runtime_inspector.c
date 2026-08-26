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
    bool on_max;

    if (rt == NULL) {
        return;
    }

    on_max = rt->machine_ready &&
        runtime_turbo_is_max_value(rt->active_turbo_multiplier) &&
        rt->history_off_on_max;

    if (!enabled) {
        if (on_max) {
            rt->inspector_enabled_saved_for_max = false;
        }
        rt->inspector_enabled = false;
        runtime_inspector_recorder_set_enabled(rt, false);
        return;
    }

    if (on_max) {
        /* Remember Record-on for leave-max; do not actually record in max. */
        rt->inspector_enabled_saved_for_max = true;
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
    runtime_history_status st;
    runtime_history_record first;
    runtime_history_record last;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (rt == NULL || rt->history == NULL) {
        return;
    }
    runtime_history_get_status(rt->history, &st);
    if (!st.available || st.record_count == 0u || st.oldest_id == 0u) {
        return;
    }
    if (!runtime_history_lookup(rt->history, st.epoch, st.oldest_id, &first) ||
        !runtime_history_lookup(rt->history, st.epoch, st.newest_id, &last)) {
        return;
    }
    out->valid = true;
    out->epoch = st.epoch;
    out->oldest_id = st.oldest_id;
    out->newest_id = st.newest_id;
    out->oldest_cycle = first.machine_cycle;
    out->newest_cycle = last.machine_cycle;

    /* D17: intersection with frame ring (if it has samples) and checkpoints. */
    {
        runtime_frame_ring_info fi;
        uint64_t cp_old = 0u;
        uint64_t cp_new = 0u;
        uint64_t cp_n = 0u;

        runtime_frame_ring_get_info(&rt->frame_ring, &fi);
        if (rt->frame_ring_memory_mb > 0u && fi.capacity > 0u && fi.count > 0u) {
            if (fi.oldest_cycle > out->oldest_cycle) {
                out->oldest_cycle = fi.oldest_cycle;
            }
            if (fi.newest_cycle < out->newest_cycle) {
                out->newest_cycle = fi.newest_cycle;
            }
        }
        runtime_inspector_checkpoint_bounds(rt, &cp_old, &cp_new, &cp_n);
        if (cp_n > 0u) {
            if (cp_old > out->oldest_cycle) {
                out->oldest_cycle = cp_old;
            }
            if (cp_new < out->newest_cycle) {
                out->newest_cycle = cp_new;
            }
        }
        if (out->oldest_cycle > out->newest_cycle) {
            memset(out, 0, sizeof(*out));
            return;
        }
        runtime_inspector_fill_window_extras(rt, out);
    }
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

void runtime_inspector_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    free(rt->inspector_now_blob);
    rt->inspector_now_blob = NULL;
    rt->inspector_now_size = 0u;
    rt->inspector_now_cycle = 0u;
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
    rt->machine.video.paint_enabled = true;
    apple2_video_paint_full_frame(&rt->machine);
    inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
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
    if (!runtime_inspector_load_nearest_checkpoint(rt, cycle)) {
        return false;
    }
    apple2_video_paint_full_frame(&rt->machine);
    inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
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
    rt->machine.video.paint_enabled = true;
    inspector_apply_live_seal(rt);
    while (apple2_cycles(&rt->machine) < target_cycle) {
        uint64_t c0 = apple2_cycles(&rt->machine);
        if (!apple2_step_cycle(&rt->machine)) {
            break;
        }
        runtime_inspector_apply_logged_inputs(
            rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
    }
    if (apple2_cycles(&rt->machine) >= live) {
        return runtime_inspector_restore_live(rt);
    }
    inspector_apply_live_seal(rt);
    runtime_inspector_sync_focus(rt);
    return true;
}

bool runtime_inspector_frame_step(runtime *rt, int direction)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;
    uint64_t here;

    if (rt == NULL || !rt->machine_ready || !rt->inspecting) {
        return false;
    }
    runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    here = apple2_cycles(&rt->machine);
    if (direction > 0) {
        if (here >= live) {
            return runtime_inspector_restore_live(rt);
        }
        (void)apple2_video_take_frame_ready(&rt->machine);
        rt->machine.video.paint_enabled = true;
        inspector_apply_live_seal(rt);
        while (apple2_cycles(&rt->machine) < live) {
            uint64_t c0 = apple2_cycles(&rt->machine);
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
            runtime_inspector_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
            if (apple2_video_take_frame_ready(&rt->machine)) {
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
    if (direction < 0) {
        uint64_t last_fr;
        uint64_t c;

        if (here <= oldest) {
            return true;
        }
        if (!runtime_inspector_load_nearest_checkpoint(rt, here - 1u)) {
            return false;
        }
        last_fr = apple2_cycles(&rt->machine);
        (void)apple2_video_take_frame_ready(&rt->machine);
        rt->machine.video.paint_enabled = true;
        inspector_apply_live_seal(rt);
        while (apple2_cycles(&rt->machine) < here) {
            uint64_t c0 = apple2_cycles(&rt->machine);
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
            runtime_inspector_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
            if (rt->machine.video.frame_ready) {
                c = apple2_cycles(&rt->machine);
                (void)apple2_video_take_frame_ready(&rt->machine);
                if (c < here) {
                    last_fr = c;
                } else {
                    break;
                }
            }
        }
        if (apple2_cycles(&rt->machine) != last_fr) {
            if (!runtime_inspector_load_nearest_checkpoint(rt, last_fr)) {
                return false;
            }
            if (!runtime_inspector_reexecute_to(rt, last_fr)) {
                return false;
            }
        }
        apple2_video_paint_full_frame(&rt->machine);
        inspector_apply_live_seal(rt);
        runtime_inspector_sync_focus(rt);
        return true;
    }
    return true;
}

runtime_inspector_enter_status runtime_inspector_enter(runtime *rt)
{
    runtime_inspector_enter_status can;
    uint8_t *now = NULL;
    size_t now_size = 0u;

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

    (void)runtime_inspector_checkpoint_take(rt);
    if (runtime_inspector_checkpoint_count(rt) == 0u) {
        return RUNTIME_INSPECTOR_ENTER_EMPTY;
    }
    if (!runtime_inspector_snapshot_machine(rt, &now, &now_size)) {
        return RUNTIME_INSPECTOR_ENTER_FAILED;
    }

    /* Stay on NOW (live). Do not SEEK / land an earlier cadence checkpoint. */
    runtime_inspector_recorder_set_enabled(rt, false);
    inspector_apply_live_seal(rt);
    rt->machine.video.paint_enabled = true;
    apple2_video_paint_full_frame(&rt->machine);

    rt->inspector_now_blob = now;
    rt->inspector_now_size = now_size;
    rt->inspector_now_cycle = apple2_cycles(&rt->machine);
    rt->inspecting = true;
    runtime_inspector_sync_focus(rt);
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
        (void)runtime_inspector_restore_blob(rt, rt->inspector_now_blob, rt->inspector_now_size);
    }
    apple2_set_replay_sealed(&rt->machine, false);
    rt->inspecting = false;
    free(rt->inspector_now_blob);
    rt->inspector_now_blob = NULL;
    rt->inspector_now_size = 0u;
    rt->inspector_now_cycle = 0u;
    if (rt->inspector_enabled) {
        runtime_inspector_recorder_set_enabled(rt, true);
    }
}


#include "runtime_timemachine.h"

#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runtime_tm_warn_zero_budget(const runtime *rt)
{
    int history_zero;
    int frame_zero;
    int tm_zero;

    if (rt == NULL || !rt->timemachine_enabled) {
        return;
    }
    history_zero = (rt->history_memory_mb == 0u);
    frame_zero = (rt->frame_ring_memory_mb == 0u);
    tm_zero = (rt->timemachine_memory_mb == 0u);
    if (!history_zero && !frame_zero && !tm_zero) {
        return;
    }
    fprintf(stderr, "a2m: timemachine=1 but");
    if (history_zero) {
        fprintf(stderr, " history_memory_mb=0");
    }
    if (history_zero && (frame_zero || tm_zero)) {
        fprintf(stderr, " and");
    }
    if (frame_zero) {
        fprintf(stderr, " frame_ring_memory_mb=0");
    }
    if ((history_zero || frame_zero) && tm_zero) {
        fprintf(stderr, " and");
    }
    if (tm_zero) {
        fprintf(stderr, " timemachine_memory_mb=0");
    }
    fprintf(stderr, "; TimeMachine window will be empty\n");
}

void runtime_tm_set_enabled(runtime *rt, bool enabled)
{
    bool was_enabled;

    if (rt == NULL) {
        return;
    }
    was_enabled = rt->timemachine_enabled;
    rt->timemachine_enabled = enabled;
    if (!enabled) {
        runtime_tm_recorder_set_enabled(rt, false);
        return;
    }
    if (was_enabled) {
        return;
    }

    if (rt->history != NULL) {
        bool on_max = rt->machine_ready &&
            rt->active_turbo_multiplier == RUNTIME_TURBO_MAX;
        if (on_max && rt->history_off_on_max) {
            /* Max owns the pause; leave-max restores. Do not undo it. */
            rt->history_paused_for_max = true;
        } else {
            uint64_t cycle = rt->machine_ready ? apple2_cycles(&rt->machine) : 0u;
            (void)runtime_history_resume(rt->history, cycle);
        }
    }
    if (rt->frame_ring_memory_mb > 0u) {
        runtime_frame_ring_set_recording(&rt->frame_ring, true);
    }
    runtime_tm_recorder_set_enabled(rt, true);
    runtime_tm_warn_zero_budget(rt);
}

bool runtime_tm_enabled(const runtime *rt)
{
    return rt != NULL && rt->timemachine_enabled;
}

uint32_t runtime_tm_memory_mb(const runtime *rt)
{
    return rt != NULL ? rt->timemachine_memory_mb : 0u;
}

void runtime_tm_window_info(const runtime *rt, runtime_tm_window *out)
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
        runtime_tm_checkpoint_bounds(rt, &cp_old, &cp_new, &cp_n);
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
        runtime_tm_fill_window_extras(rt, out);
    }
}

void runtime_tm_get_focus(const runtime *rt, runtime_tm_focus *out)
{
    if (out == NULL) {
        return;
    }
    if (rt == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = rt->tm_focus;
}

runtime_tm_mode runtime_tm_current_mode(const runtime *rt)
{
    return (rt != NULL && rt->tm_forensic) ?
        RUNTIME_TM_MODE_FORENSIC : RUNTIME_TM_MODE_LIVE;
}

bool runtime_tm_in_forensic(const runtime *rt)
{
    return rt != NULL && rt->tm_forensic;
}

const char *runtime_tm_window_start_name(runtime_history_media_change_kind kind)
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

bool runtime_tm_snapshot_machine(runtime *rt, uint8_t **blob, size_t *size)
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

bool runtime_tm_restore_blob(runtime *rt, const uint8_t *blob, size_t size)
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

void runtime_tm_forensic_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    free(rt->tm_now_blob);
    rt->tm_now_blob = NULL;
    rt->tm_now_size = 0u;
    rt->tm_now_cycle = 0u;
    rt->tm_forensic = false;
}

uint64_t runtime_tm_live_cycle(const runtime *rt)
{
    uint64_t oldest = 0u;
    uint64_t newest_cp = 0u;
    uint64_t count = 0u;
    uint64_t live;

    if (rt == NULL) {
        return 0u;
    }
    if (rt->tm_forensic && rt->tm_now_blob != NULL) {
        live = rt->tm_now_cycle;
    } else if (rt->machine_ready) {
        live = apple2_cycles(&rt->machine);
    } else {
        live = 0u;
    }
    runtime_tm_checkpoint_bounds(rt, &oldest, &newest_cp, &count);
    if (count > 0u && newest_cp > live) {
        live = newest_cp;
    }
    return live;
}

void runtime_tm_timeline_bounds(
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
    runtime_tm_checkpoint_bounds(rt, &cp_old, &cp_new, &n);
    if (n == 0u) {
        return;
    }
    if (oldest != NULL) {
        *oldest = cp_old;
    }
    if (live != NULL) {
        *live = runtime_tm_live_cycle(rt);
    }
    if (count != NULL) {
        *count = n;
    }
}

bool runtime_tm_at_live(const runtime *rt)
{
    uint64_t live;

    if (rt == NULL || !rt->tm_forensic || !rt->machine_ready) {
        return false;
    }
    live = rt->tm_now_cycle;
    if (live == 0u) {
        live = runtime_tm_live_cycle(rt);
    }
    return apple2_cycles(&rt->machine) >= live;
}

void runtime_tm_sync_focus(runtime *rt)
{
    if (rt == NULL || !rt->machine_ready) {
        return;
    }
    memset(&rt->tm_focus, 0, sizeof(rt->tm_focus));
    rt->tm_focus.valid = true;
    rt->tm_focus.cycle = apple2_cycles(&rt->machine);
    rt->tm_focus.pc = rt->machine.cpu.cpu.pc;
    rt->tm_focus.a = rt->machine.cpu.cpu.A;
    rt->tm_focus.x = rt->machine.cpu.cpu.X;
    rt->tm_focus.y = rt->machine.cpu.cpu.Y;
    rt->tm_focus.p = rt->machine.cpu.cpu.flags;
    rt->tm_focus.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xffu);
}

runtime_tm_enter_status runtime_tm_can_enter(const runtime *rt)
{
    if (rt == NULL || !rt->machine_ready) {
        return RUNTIME_TM_ENTER_UNAVAILABLE;
    }
    if (rt->tm_forensic) {
        return RUNTIME_TM_ENTER_OK;
    }
    if (!rt->timemachine_enabled) {
        return RUNTIME_TM_ENTER_UNAVAILABLE;
    }
    if (runtime_tm_checkpoint_count(rt) == 0u) {
        return RUNTIME_TM_ENTER_EMPTY;
    }
    return RUNTIME_TM_ENTER_OK;
}

static void tm_apply_live_seal(runtime *rt)
{
    apple2_set_replay_sealed(&rt->machine, true);
    apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    apple2_set_memory_access_callback(&rt->machine, NULL, NULL);
}

bool runtime_tm_materialize_live(runtime *rt, uint64_t cycle)
{
    bool ok;

    if (rt == NULL || !rt->machine_ready) {
        return false;
    }
    tm_apply_live_seal(rt);
    ok = runtime_tm_materialize(rt, cycle, &rt->machine);
    /* Scratch materialize clears the seal; forensic stays sealed. */
    tm_apply_live_seal(rt);
    apple2_video_reseed_from_cycles(&rt->machine);
    return ok;
}

bool runtime_tm_restore_live(runtime *rt)
{
    if (rt == NULL || rt->tm_now_blob == NULL || rt->tm_now_size == 0u) {
        return false;
    }
    if (!runtime_tm_restore_blob(rt, rt->tm_now_blob, rt->tm_now_size)) {
        return false;
    }
    rt->machine.video.paint_enabled = true;
    apple2_video_paint_full_frame(&rt->machine);
    tm_apply_live_seal(rt);
    runtime_tm_sync_focus(rt);
    return true;
}

bool runtime_tm_land(runtime *rt, uint64_t cycle)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;

    if (rt == NULL || !rt->machine_ready || !rt->tm_forensic) {
        return false;
    }
    runtime_tm_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    if (cycle >= live) {
        return runtime_tm_restore_live(rt);
    }
    if (cycle < oldest) {
        cycle = oldest;
    }
    if (!runtime_tm_load_nearest_checkpoint(rt, cycle)) {
        return false;
    }
    apple2_video_paint_full_frame(&rt->machine);
    tm_apply_live_seal(rt);
    runtime_tm_sync_focus(rt);
    return true;
}

bool runtime_tm_reexecute_to(runtime *rt, uint64_t target_cycle)
{
    uint64_t live;

    if (rt == NULL || !rt->machine_ready || !rt->tm_forensic) {
        return false;
    }
    live = runtime_tm_live_cycle(rt);
    if (target_cycle > live) {
        target_cycle = live;
    }
    rt->machine.video.paint_enabled = true;
    tm_apply_live_seal(rt);
    while (apple2_cycles(&rt->machine) < target_cycle) {
        uint64_t c0 = apple2_cycles(&rt->machine);
        if (!apple2_step_cycle(&rt->machine)) {
            break;
        }
        runtime_tm_apply_logged_inputs(
            rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
    }
    if (apple2_cycles(&rt->machine) >= live) {
        return runtime_tm_restore_live(rt);
    }
    tm_apply_live_seal(rt);
    runtime_tm_sync_focus(rt);
    return true;
}

bool runtime_tm_frame_step(runtime *rt, int direction)
{
    uint64_t oldest = 0u;
    uint64_t live = 0u;
    uint64_t count = 0u;
    uint64_t here;

    if (rt == NULL || !rt->machine_ready || !rt->tm_forensic) {
        return false;
    }
    runtime_tm_timeline_bounds(rt, &oldest, &live, &count);
    if (count == 0u) {
        return false;
    }
    here = apple2_cycles(&rt->machine);
    if (direction > 0) {
        if (here >= live) {
            return runtime_tm_restore_live(rt);
        }
        (void)apple2_video_take_frame_ready(&rt->machine);
        rt->machine.video.paint_enabled = true;
        tm_apply_live_seal(rt);
        while (apple2_cycles(&rt->machine) < live) {
            uint64_t c0 = apple2_cycles(&rt->machine);
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
            runtime_tm_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
            if (apple2_video_take_frame_ready(&rt->machine)) {
                break;
            }
        }
        if (apple2_cycles(&rt->machine) >= live) {
            return runtime_tm_restore_live(rt);
        }
        tm_apply_live_seal(rt);
        runtime_tm_sync_focus(rt);
        return true;
    }
    if (direction < 0) {
        uint64_t last_fr;
        uint64_t c;

        if (here <= oldest) {
            return true;
        }
        if (!runtime_tm_load_nearest_checkpoint(rt, here - 1u)) {
            return false;
        }
        last_fr = apple2_cycles(&rt->machine);
        (void)apple2_video_take_frame_ready(&rt->machine);
        rt->machine.video.paint_enabled = true;
        tm_apply_live_seal(rt);
        while (apple2_cycles(&rt->machine) < here) {
            uint64_t c0 = apple2_cycles(&rt->machine);
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
            runtime_tm_apply_logged_inputs(
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
            if (!runtime_tm_load_nearest_checkpoint(rt, last_fr)) {
                return false;
            }
            if (!runtime_tm_reexecute_to(rt, last_fr)) {
                return false;
            }
        }
        apple2_video_paint_full_frame(&rt->machine);
        tm_apply_live_seal(rt);
        runtime_tm_sync_focus(rt);
        return true;
    }
    return true;
}

runtime_tm_enter_status runtime_tm_enter_forensic(runtime *rt)
{
    runtime_tm_enter_status can;
    uint8_t *now = NULL;
    size_t now_size = 0u;

    if (rt == NULL || !rt->machine_ready) {
        return RUNTIME_TM_ENTER_UNAVAILABLE;
    }
    if (rt->tm_forensic) {
        return RUNTIME_TM_ENTER_OK;
    }
    can = runtime_tm_can_enter(rt);
    if (can != RUNTIME_TM_ENTER_OK) {
        return can;
    }

    (void)runtime_tm_checkpoint_take(rt);
    if (runtime_tm_checkpoint_count(rt) == 0u) {
        return RUNTIME_TM_ENTER_EMPTY;
    }
    if (!runtime_tm_snapshot_machine(rt, &now, &now_size)) {
        return RUNTIME_TM_ENTER_FAILED;
    }

    /* Stay on NOW (live). Do not SEEK / land an earlier cadence checkpoint. */
    runtime_tm_recorder_set_enabled(rt, false);
    tm_apply_live_seal(rt);
    rt->machine.video.paint_enabled = true;
    apple2_video_paint_full_frame(&rt->machine);

    rt->tm_now_blob = now;
    rt->tm_now_size = now_size;
    rt->tm_now_cycle = apple2_cycles(&rt->machine);
    rt->tm_forensic = true;
    runtime_tm_sync_focus(rt);
    return RUNTIME_TM_ENTER_OK;
}

void runtime_tm_exit_forensic(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    if (!rt->tm_forensic) {
        return;
    }
    if (rt->tm_now_blob != NULL && rt->tm_now_size > 0u) {
        (void)runtime_tm_restore_blob(rt, rt->tm_now_blob, rt->tm_now_size);
    }
    apple2_set_replay_sealed(&rt->machine, false);
    rt->tm_forensic = false;
    free(rt->tm_now_blob);
    rt->tm_now_blob = NULL;
    rt->tm_now_size = 0u;
    rt->tm_now_cycle = 0u;
    if (rt->timemachine_enabled) {
        runtime_tm_recorder_set_enabled(rt, true);
    }
}


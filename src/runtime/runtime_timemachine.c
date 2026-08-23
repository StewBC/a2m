#include "runtime_timemachine.h"

#include "apple2.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"

#include <stdio.h>

static void runtime_tm_warn_zero_budget(const runtime *rt)
{
    int history_zero;
    int frame_zero;

    if (rt == NULL || !rt->timemachine_enabled) {
        return;
    }
    history_zero = (rt->history_memory_mb == 0u);
    frame_zero = (rt->frame_ring_memory_mb == 0u);
    if (!history_zero && !frame_zero) {
        return;
    }
    fprintf(stderr, "a2m: timemachine=1 but");
    if (history_zero) {
        fprintf(stderr, " history_memory_mb=0");
    }
    if (history_zero && frame_zero) {
        fprintf(stderr, " and");
    }
    if (frame_zero) {
        fprintf(stderr, " frame_ring_memory_mb=0");
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
    if (!enabled || was_enabled) {
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

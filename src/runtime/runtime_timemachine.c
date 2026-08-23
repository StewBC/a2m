#include "runtime_timemachine.h"

#include "apple2.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"

#include <stdio.h>
#include <string.h>

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

static void tm_focus_from_record(
    const runtime_history_record *record,
    runtime_tm_focus *out)
{
    memset(out, 0, sizeof(*out));
    if (record == NULL) {
        return;
    }
    out->valid = true;
    out->kind = record->kind;
    out->opcode = record->opcode;
    out->sp = record->sp;
    out->a = record->a;
    out->x = record->x;
    out->y = record->y;
    out->p = record->p;
    out->pc = record->pc;
    out->timeline = record->timeline;
    out->epoch = record->epoch;
    out->history_id = record->id;
    out->cycle = record->machine_cycle;
}

static bool tm_is_insn(const runtime_history_record *record)
{
    return record != NULL &&
        record->kind == RUNTIME_HISTORY_RECORD_INSTRUCTION;
}

static bool tm_is_epoch_barrier(const runtime_history_record *record)
{
    return record != NULL &&
        record->kind == RUNTIME_HISTORY_RECORD_MARKER &&
        (record->marker_kind == RUNTIME_HISTORY_MARKER_RESET_COMPLETE ||
         record->marker_kind == RUNTIME_HISTORY_MARKER_STATE_LOAD);
}

static bool tm_sp_wrapped(uint8_t a, uint8_t b)
{
    uint8_t d1 = (uint8_t)(a - b);
    uint8_t d2 = (uint8_t)(b - a);
    uint8_t d = d1 < d2 ? d1 : d2;
    return d > 3u;
}

static bool tm_id_in_window(const runtime_tm_window *window, uint64_t id)
{
    return window->valid && id >= window->oldest_id && id <= window->newest_id;
}

static runtime_tm_query_status tm_lookup_in_window(
    const runtime *rt,
    const runtime_tm_window *window,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record)
{
    if (!window->valid) {
        return RUNTIME_TM_QUERY_EMPTY;
    }
    if (epoch != window->epoch) {
        return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
    }
    if (!tm_id_in_window(window, id)) {
        return RUNTIME_TM_QUERY_NOT_RETAINED;
    }
    if (!runtime_history_lookup(rt->history, epoch, id, out_record)) {
        return RUNTIME_TM_QUERY_NOT_RETAINED;
    }
    if (out_record->epoch != window->epoch) {
        return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
    }
    return RUNTIME_TM_QUERY_OK;
}

static bool tm_walk(
    const runtime *rt,
    uint64_t epoch,
    uint64_t id,
    int direction,
    runtime_history_record *out_record)
{
    if (direction > 0) {
        return runtime_history_next(rt->history, epoch, id, out_record);
    }
    return runtime_history_previous(rt->history, epoch, id, out_record);
}

/*
 * Walk to the next instruction in `direction`. Barrier markers (reset / state
 * load) between the start id and the candidate reject the walk. Starting ON a
 * barrier is allowed (step off it). IRQ/NMI/other markers are skipped.
 */
static runtime_tm_query_status tm_next_insn(
    const runtime *rt,
    const runtime_tm_window *window,
    const runtime_history_record *start,
    int direction,
    runtime_history_record *out_record)
{
    runtime_history_record cur;
    uint64_t start_id;

    if (start == NULL || direction == 0) {
        return RUNTIME_TM_QUERY_INVALID;
    }
    start_id = start->id;
    cur = *start;
    for (;;) {
        if (!tm_walk(rt, window->epoch, cur.id, direction, &cur)) {
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (!tm_id_in_window(window, cur.id)) {
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (cur.epoch != window->epoch) {
            return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
        }
        if (tm_is_epoch_barrier(&cur) && cur.id != start_id) {
            return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
        }
        if (tm_is_insn(&cur)) {
            *out_record = cur;
            return RUNTIME_TM_QUERY_OK;
        }
    }
}

static runtime_tm_query_status tm_newest_insn(
    const runtime *rt,
    const runtime_tm_window *window,
    runtime_history_record *out_record)
{
    runtime_history_record cur;
    runtime_tm_query_status st;

    st = tm_lookup_in_window(
        rt, window, window->epoch, window->newest_id, &cur);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    if (tm_is_insn(&cur)) {
        *out_record = cur;
        return RUNTIME_TM_QUERY_OK;
    }
    return tm_next_insn(rt, window, &cur, -1, out_record);
}

static runtime_tm_query_status tm_oldest_insn(
    const runtime *rt,
    const runtime_tm_window *window,
    runtime_history_record *out_record)
{
    runtime_history_record cur;
    runtime_tm_query_status st;

    st = tm_lookup_in_window(
        rt, window, window->epoch, window->oldest_id, &cur);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    if (tm_is_insn(&cur)) {
        *out_record = cur;
        return RUNTIME_TM_QUERY_OK;
    }
    return tm_next_insn(rt, window, &cur, 1, out_record);
}

static runtime_tm_query_status tm_ensure_focus_record(
    runtime *rt,
    const runtime_tm_window *window,
    runtime_history_record *out_record)
{
    runtime_tm_query_status st;

    if (rt->tm_focus.valid) {
        if (rt->tm_focus.epoch != window->epoch) {
            return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
        }
        st = tm_lookup_in_window(
            rt, window, rt->tm_focus.epoch, rt->tm_focus.history_id, out_record);
        if (st == RUNTIME_TM_QUERY_OK) {
            return RUNTIME_TM_QUERY_OK;
        }
    }
    st = tm_newest_insn(rt, window, out_record);
    if (st == RUNTIME_TM_QUERY_OK) {
        tm_focus_from_record(out_record, &rt->tm_focus);
    }
    return st;
}

static void tm_commit_focus(
    runtime *rt,
    const runtime_history_record *record,
    runtime_tm_query_result *result,
    runtime_tm_query_status status,
    bool clamped)
{
    tm_focus_from_record(record, &rt->tm_focus);
    result->status = status;
    result->focus = rt->tm_focus;
    result->clamped = clamped;
}

static runtime_tm_query_status tm_seek_id(
    runtime *rt,
    const runtime_tm_window *window,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *result)
{
    uint64_t epoch = args->epoch != 0u ? args->epoch : window->epoch;
    uint64_t id = args->history_id;
    runtime_history_record rec;
    runtime_tm_query_status st;
    bool clamped = false;

    if (epoch != window->epoch) {
        return RUNTIME_TM_QUERY_EPOCH_MISMATCH;
    }
    if (id < window->oldest_id) {
        id = window->oldest_id;
        clamped = true;
    } else if (id > window->newest_id) {
        id = window->newest_id;
        clamped = true;
    }
    st = tm_lookup_in_window(rt, window, epoch, id, &rec);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, clamped);
    return RUNTIME_TM_QUERY_OK;
}

static runtime_tm_query_status tm_seek_cycle(
    runtime *rt,
    const runtime_tm_window *window,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *result)
{
    runtime_history_record rec;
    runtime_history_record candidate;
    runtime_tm_query_status st;
    uint64_t target = args->cycle;
    bool clamped = false;

    if (target < window->oldest_cycle) {
        target = window->oldest_cycle;
        clamped = true;
    } else if (target > window->newest_cycle) {
        target = window->newest_cycle;
        clamped = true;
    }

    st = tm_lookup_in_window(
        rt, window, window->epoch, window->newest_id, &rec);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    candidate = rec;
    while (rec.machine_cycle > target) {
        if (!runtime_history_previous(
                rt->history, window->epoch, rec.id, &rec) ||
            !tm_id_in_window(window, rec.id)) {
            st = tm_oldest_insn(rt, window, &candidate);
            if (st != RUNTIME_TM_QUERY_OK) {
                return st;
            }
            tm_commit_focus(rt, &candidate, result, RUNTIME_TM_QUERY_OK, true);
            return RUNTIME_TM_QUERY_OK;
        }
        candidate = rec;
    }
    if (!tm_is_insn(&candidate)) {
        st = tm_next_insn(rt, window, &candidate, -1, &rec);
        if (st == RUNTIME_TM_QUERY_OK) {
            candidate = rec;
        } else {
            st = tm_next_insn(rt, window, &candidate, 1, &rec);
            if (st != RUNTIME_TM_QUERY_OK) {
                return st;
            }
            candidate = rec;
        }
    }
    tm_commit_focus(rt, &candidate, result, RUNTIME_TM_QUERY_OK, clamped);
    return RUNTIME_TM_QUERY_OK;
}

static runtime_tm_query_status tm_step(
    runtime *rt,
    const runtime_tm_window *window,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *result)
{
    runtime_history_record start;
    runtime_history_record rec;
    runtime_tm_query_status st;
    int direction = args->direction < 0 ? -1 : 1;

    st = tm_ensure_focus_record(rt, window, &start);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    st = tm_next_insn(rt, window, &start, direction, &rec);
    if (st == RUNTIME_TM_QUERY_END_OF_TAPE) {
        tm_commit_focus(rt, &start, result, RUNTIME_TM_QUERY_END_OF_TAPE, false);
        return RUNTIME_TM_QUERY_END_OF_TAPE;
    }
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, false);
    return RUNTIME_TM_QUERY_OK;
}

static runtime_tm_query_status tm_step_over(
    runtime *rt,
    const runtime_tm_window *window,
    runtime_tm_query_result *result)
{
    runtime_history_record start;
    runtime_history_record rec;
    runtime_history_record last_insn;
    runtime_tm_query_status st;
    uint8_t entry_sp;
    uint8_t prev_sp;
    runtime_tm_query_args step_args;

    st = tm_ensure_focus_record(rt, window, &start);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    if (!tm_is_insn(&start) || start.opcode != RUNTIME_TM_OPCODE_JSR) {
        memset(&step_args, 0, sizeof(step_args));
        step_args.direction = 1;
        return tm_step(rt, window, &step_args, result);
    }
    if (start.sp < 2u) {
        return RUNTIME_TM_QUERY_SP_WRAP;
    }
    entry_sp = start.sp;
    prev_sp = start.sp;
    last_insn = start;
    rec = start;
    for (;;) {
        st = tm_next_insn(rt, window, &rec, 1, &rec);
        if (st == RUNTIME_TM_QUERY_END_OF_TAPE) {
            tm_commit_focus(
                rt, &last_insn, result, RUNTIME_TM_QUERY_END_OF_TAPE, false);
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (st != RUNTIME_TM_QUERY_OK) {
            return st;
        }
        if (tm_sp_wrapped(prev_sp, rec.sp)) {
            return RUNTIME_TM_QUERY_SP_WRAP;
        }
        last_insn = rec;
        if (rec.sp >= entry_sp) {
            tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, false);
            return RUNTIME_TM_QUERY_OK;
        }
        prev_sp = rec.sp;
    }
}

static runtime_tm_query_status tm_step_out(
    runtime *rt,
    const runtime_tm_window *window,
    runtime_tm_query_result *result)
{
    runtime_history_record start;
    runtime_history_record rec;
    runtime_history_record last_insn;
    runtime_tm_query_status st;
    uint8_t entry_sp;
    uint8_t prev_sp;

    st = tm_ensure_focus_record(rt, window, &start);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    if (!tm_is_insn(&start)) {
        st = tm_next_insn(rt, window, &start, 1, &start);
        if (st != RUNTIME_TM_QUERY_OK) {
            return st;
        }
    }
    entry_sp = start.sp;
    prev_sp = start.sp;
    last_insn = start;
    rec = start;
    for (;;) {
        st = tm_next_insn(rt, window, &rec, 1, &rec);
        if (st == RUNTIME_TM_QUERY_END_OF_TAPE) {
            tm_commit_focus(
                rt, &last_insn, result, RUNTIME_TM_QUERY_END_OF_TAPE, false);
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (st != RUNTIME_TM_QUERY_OK) {
            return st;
        }
        if (tm_sp_wrapped(prev_sp, rec.sp)) {
            return RUNTIME_TM_QUERY_SP_WRAP;
        }
        last_insn = rec;
        if (rec.sp > entry_sp) {
            tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, false);
            return RUNTIME_TM_QUERY_OK;
        }
        prev_sp = rec.sp;
    }
}

static runtime_tm_query_status tm_run_to_pc(
    runtime *rt,
    const runtime_tm_window *window,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *result)
{
    runtime_history_record start;
    runtime_history_record rec;
    runtime_history_record last_insn;
    runtime_tm_query_status st;

    st = tm_ensure_focus_record(rt, window, &start);
    if (st != RUNTIME_TM_QUERY_OK) {
        return st;
    }
    rec = start;
    last_insn = start;
    if (tm_is_insn(&rec) && rec.pc == args->target_pc) {
        tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, false);
        return RUNTIME_TM_QUERY_OK;
    }
    for (;;) {
        st = tm_next_insn(rt, window, &rec, 1, &rec);
        if (st == RUNTIME_TM_QUERY_END_OF_TAPE) {
            tm_commit_focus(
                rt, &last_insn, result, RUNTIME_TM_QUERY_END_OF_TAPE, false);
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (st != RUNTIME_TM_QUERY_OK) {
            return st;
        }
        last_insn = rec;
        if (args->cycle_ceiling != 0u && rec.machine_cycle > args->cycle_ceiling) {
            tm_commit_focus(
                rt, &last_insn, result, RUNTIME_TM_QUERY_END_OF_TAPE, false);
            return RUNTIME_TM_QUERY_END_OF_TAPE;
        }
        if (rec.pc == args->target_pc) {
            tm_commit_focus(rt, &rec, result, RUNTIME_TM_QUERY_OK, false);
            return RUNTIME_TM_QUERY_OK;
        }
    }
}

runtime_tm_query_status runtime_tm_query(
    runtime *rt,
    runtime_tm_query_op op,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *out_result)
{
    runtime_tm_window window;
    runtime_tm_query_args zero_args;
    runtime_tm_query_result local;
    runtime_tm_query_result *result;
    runtime_tm_query_status st;

    result = out_result != NULL ? out_result : &local;
    memset(result, 0, sizeof(*result));
    if (args == NULL) {
        memset(&zero_args, 0, sizeof(zero_args));
        args = &zero_args;
    }
    if (rt == NULL || rt->history == NULL) {
        result->status = RUNTIME_TM_QUERY_UNAVAILABLE;
        return result->status;
    }
    runtime_tm_window_info(rt, &window);
    if (!window.valid) {
        result->status = RUNTIME_TM_QUERY_EMPTY;
        return result->status;
    }

    switch (op) {
    case RUNTIME_TM_QUERY_SEEK_ID:
        st = tm_seek_id(rt, &window, args, result);
        break;
    case RUNTIME_TM_QUERY_SEEK_CYCLE:
        st = tm_seek_cycle(rt, &window, args, result);
        break;
    case RUNTIME_TM_QUERY_STEP:
        st = tm_step(rt, &window, args, result);
        break;
    case RUNTIME_TM_QUERY_STEP_OVER:
        st = tm_step_over(rt, &window, result);
        break;
    case RUNTIME_TM_QUERY_STEP_OUT:
        st = tm_step_out(rt, &window, result);
        break;
    case RUNTIME_TM_QUERY_RUN_TO_PC:
        st = tm_run_to_pc(rt, &window, args, result);
        break;
    default:
        st = RUNTIME_TM_QUERY_INVALID;
        break;
    }
    if (st != RUNTIME_TM_QUERY_OK && st != RUNTIME_TM_QUERY_END_OF_TAPE) {
        result->status = st;
        result->focus = rt->tm_focus;
        result->clamped = false;
    }
    return result->status;
}

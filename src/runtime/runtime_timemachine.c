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

static void tm_bp_range(
    const runtime_breakpoint *bp, uint16_t *lo, uint16_t *hi)
{
    uint16_t a = bp->start_address;
    uint16_t b = bp->has_end_address ? bp->end_address : bp->start_address;

    if (a <= b) {
        *lo = a;
        *hi = b;
    } else {
        *lo = b;
        *hi = a;
    }
}

static bool tm_bp_find_next(
    runtime *rt,
    const runtime_tm_window *window,
    const runtime_breakpoint *bp,
    uint64_t from_id,
    runtime_history_record *out)
{
    runtime_history_query query;
    runtime_history_record recs[1];
    runtime_history_page page;
    uint16_t lo;
    uint16_t hi;

    if (rt == NULL || rt->history == NULL || bp == NULL || out == NULL ||
        window == NULL || !bp->enabled || bp->condition.term_count > 0u) {
        return false;
    }
    tm_bp_range(bp, &lo, &hi);
    memset(&query, 0, sizeof(query));
    query.direction = RUNTIME_HISTORY_QUERY_FORWARD;
    query.has_epoch = true;
    query.epoch = window->epoch;

    if ((bp->access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0u) {
        query.has_pc = true;
        query.pc_first = lo;
        query.pc_last = hi;
    } else if ((bp->access_mask & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0u) {
        query.has_address = true;
        query.address_first = lo;
        query.address_last = hi;
        query.has_access = true;
        query.access_mask = RUNTIME_HISTORY_ACCESS_DATA_WRITE;
    } else {
        return false;
    }

    memset(&page, 0, sizeof(page));
    if (runtime_history_find(
            rt->history, &query, from_id, 1u, recs, &page, NULL) !=
            RUNTIME_HISTORY_QUERY_OK ||
        page.count == 0u) {
        return false;
    }
    *out = recs[0];
    return true;
}

static runtime_tm_query_status tm_run_until_break(
    runtime *rt,
    const runtime_tm_window *window,
    runtime_tm_query_result *result)
{
    runtime_history_record best;
    int have = 0;
    size_t i;
    uint64_t from_id;

    if (!rt->tm_forensic) {
        return RUNTIME_TM_QUERY_UNAVAILABLE;
    }
    from_id = rt->tm_focus.valid ?
        rt->tm_focus.history_id + 1u : window->oldest_id;
    if (from_id == 0u || from_id > window->newest_id) {
        result->focus = rt->tm_focus;
        result->status = RUNTIME_TM_QUERY_END_OF_TAPE;
        result->clamped = false;
        return RUNTIME_TM_QUERY_END_OF_TAPE;
    }

    memset(&best, 0, sizeof(best));
    for (i = 0; i < rt->tm_breakpoint_count; ++i) {
        runtime_history_record rec;
        const runtime_breakpoint *bp = &rt->tm_breakpoints[i];

        if ((bp->access_mask &
             (RUNTIME_BREAKPOINT_ACCESS_EXECUTE |
              RUNTIME_BREAKPOINT_ACCESS_WRITE)) ==
            (RUNTIME_BREAKPOINT_ACCESS_EXECUTE |
             RUNTIME_BREAKPOINT_ACCESS_WRITE)) {
            runtime_breakpoint exec_bp = *bp;
            runtime_breakpoint write_bp = *bp;
            exec_bp.access_mask = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
            write_bp.access_mask = RUNTIME_BREAKPOINT_ACCESS_WRITE;
            if (tm_bp_find_next(rt, window, &exec_bp, from_id, &rec)) {
                if (!have || rec.id < best.id) {
                    best = rec;
                    have = 1;
                }
            }
            if (tm_bp_find_next(rt, window, &write_bp, from_id, &rec)) {
                if (!have || rec.id < best.id) {
                    best = rec;
                    have = 1;
                }
            }
            continue;
        }
        if (tm_bp_find_next(rt, window, bp, from_id, &rec)) {
            if (!have || rec.id < best.id) {
                best = rec;
                have = 1;
            }
        }
    }
    if (!have) {
        result->focus = rt->tm_focus;
        result->status = RUNTIME_TM_QUERY_END_OF_TAPE;
        result->clamped = false;
        return RUNTIME_TM_QUERY_END_OF_TAPE;
    }
    tm_commit_focus(rt, &best, result, RUNTIME_TM_QUERY_OK, false);
    return RUNTIME_TM_QUERY_OK;
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
    case RUNTIME_TM_QUERY_RUN_UNTIL_BREAK:
        st = tm_run_until_break(rt, &window, result);
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

static int tm_bp_find_id(const runtime *rt, uint32_t id)
{
    size_t i;

    if (rt == NULL) {
        return -1;
    }
    for (i = 0; i < rt->tm_breakpoint_count; ++i) {
        if (rt->tm_breakpoints[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool tm_bp_definition_ok(const runtime_breakpoint_definition *definition)
{
    uint32_t access;

    if (definition == NULL) {
        return false;
    }
    access = definition->access &
        (RUNTIME_BREAKPOINT_ACCESS_EXECUTE |
         RUNTIME_BREAKPOINT_ACCESS_READ |
         RUNTIME_BREAKPOINT_ACCESS_WRITE);
    if (access == 0u) {
        return false;
    }
    /* Index-scan kinds: execute and write. Read is stored but not evaluated. */
    return true;
}

static void tm_bp_apply(
    runtime_breakpoint *bp,
    const runtime_breakpoint_definition *definition)
{
    bp->enabled = definition->enabled != 0;
    bp->start_address = definition->start_address;
    bp->end_address = definition->has_end_address ?
        definition->end_address : definition->start_address;
    bp->has_end_address = definition->has_end_address != 0;
    bp->access_mask = definition->access;
    bp->mapping = definition->mapping;
    bp->action_mask = definition->actions != 0u ?
        definition->actions : (uint32_t)RUNTIME_BREAKPOINT_ACTION_BREAK;
    bp->use_counter = definition->use_counter != 0;
    bp->initial_count = definition->initial_count;
    bp->reset_count = definition->reset_count;
    bp->counter = definition->initial_count;
    bp->current_hits = 0;
    bp->condition = definition->condition;
}

bool runtime_tm_bp_add(
    runtime *rt,
    const runtime_breakpoint_definition *definition,
    uint32_t *out_id)
{
    runtime_breakpoint *bp;

    if (rt == NULL || !tm_bp_definition_ok(definition)) {
        return false;
    }
    if (rt->tm_breakpoint_count >= RUNTIME_BREAKPOINT_CAPACITY) {
        return false;
    }
    if (rt->tm_next_breakpoint_id == 0u) {
        rt->tm_next_breakpoint_id = 1u;
    }
    bp = &rt->tm_breakpoints[rt->tm_breakpoint_count];
    memset(bp, 0, sizeof(*bp));
    bp->id = rt->tm_next_breakpoint_id++;
    tm_bp_apply(bp, definition);
    rt->tm_breakpoint_count++;
    if (out_id != NULL) {
        *out_id = bp->id;
    }
    return true;
}

bool runtime_tm_bp_update(
    runtime *rt,
    uint32_t id,
    const runtime_breakpoint_definition *definition)
{
    int index;

    if (rt == NULL || !tm_bp_definition_ok(definition)) {
        return false;
    }
    index = tm_bp_find_id(rt, id);
    if (index < 0) {
        return false;
    }
    tm_bp_apply(&rt->tm_breakpoints[index], definition);
    return true;
}

bool runtime_tm_bp_clear(runtime *rt, uint32_t id)
{
    int index;

    if (rt == NULL) {
        return false;
    }
    index = tm_bp_find_id(rt, id);
    if (index < 0) {
        return false;
    }
    if ((size_t)index + 1u < rt->tm_breakpoint_count) {
        memmove(
            &rt->tm_breakpoints[index],
            &rt->tm_breakpoints[index + 1],
            (rt->tm_breakpoint_count - (size_t)index - 1u) *
                sizeof(rt->tm_breakpoints[0]));
    }
    rt->tm_breakpoint_count--;
    memset(
        &rt->tm_breakpoints[rt->tm_breakpoint_count],
        0,
        sizeof(rt->tm_breakpoints[0]));
    return true;
}

void runtime_tm_bp_clear_all(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    memset(rt->tm_breakpoints, 0, sizeof(rt->tm_breakpoints));
    rt->tm_breakpoint_count = 0u;
}

bool runtime_tm_bp_set_enabled(runtime *rt, uint32_t id, bool enabled)
{
    int index;

    if (rt == NULL) {
        return false;
    }
    index = tm_bp_find_id(rt, id);
    if (index < 0) {
        return false;
    }
    rt->tm_breakpoints[index].enabled = enabled;
    return true;
}

void runtime_tm_bp_toggle_execute(runtime *rt, uint16_t address)
{
    size_t i;
    runtime_breakpoint_definition def;

    if (rt == NULL) {
        return;
    }
    for (i = 0; i < rt->tm_breakpoint_count; ++i) {
        runtime_breakpoint *bp = &rt->tm_breakpoints[i];
        if (bp->start_address == address &&
            !bp->has_end_address &&
            (bp->access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0u) {
            (void)runtime_tm_bp_clear(rt, bp->id);
            return;
        }
    }
    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = address;
    def.end_address = address;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    (void)runtime_tm_bp_add(rt, &def, NULL);
}

void runtime_tm_bp_fill_snapshot(
    const runtime *rt, runtime_breakpoint_snapshot *out)
{
    size_t i;
    size_t n;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (rt == NULL) {
        return;
    }
    n = rt->tm_breakpoint_count;
    if (n > RUNTIME_BREAKPOINT_SNAPSHOT_MAX) {
        n = RUNTIME_BREAKPOINT_SNAPSHOT_MAX;
    }
    out->count = (uint16_t)n;
    for (i = 0; i < n; ++i) {
        runtime_breakpoint_snapshot_entry *e = &out->entries[i];
        const runtime_breakpoint *bp = &rt->tm_breakpoints[i];

        e->id = bp->id;
        e->start_address = bp->start_address;
        e->end_address = bp->end_address;
        e->has_end_address = bp->has_end_address ? 1u : 0u;
        e->access = (runtime_breakpoint_access)bp->access_mask;
        e->mapping = bp->mapping;
        e->actions = bp->action_mask;
        e->enabled = bp->enabled ? 1u : 0u;
        e->use_counter = bp->use_counter ? 1u : 0u;
        e->current_hits = bp->current_hits;
        e->initial_count = bp->initial_count;
        e->reset_count = bp->reset_count;
        e->counter = bp->counter;
        e->condition = bp->condition;
        e->address = bp->start_address;
        e->target_hits = bp->initial_count;
    }
}

size_t runtime_tm_bp_count(const runtime *rt)
{
    return rt != NULL ? rt->tm_breakpoint_count : 0u;
}

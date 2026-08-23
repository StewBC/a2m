#include "runtime_timemachine.h"

#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "video.h"

#include <stdlib.h>
#include <string.h>

enum { RUNTIME_TM_INPUT_CAP_DEFAULT = 65536u };

typedef struct runtime_tm_checkpoint {
    uint64_t cycle;
    uint64_t epoch;
    size_t size;
    uint8_t *blob;
} runtime_tm_checkpoint;

typedef struct runtime_tm_input {
    uint64_t cycle;
    uint8_t kind;
    uint8_t a;
    uint8_t b;
    uint8_t c;
} runtime_tm_input;

struct runtime_tm_recorder {
    bool recording;
    uint32_t cadence_cycles;
    size_t memory_budget;
    size_t used;
    uint32_t slot_count;
    uint32_t count;
    uint32_t head;
    runtime_tm_checkpoint *slots;
    uint64_t last_checkpoint_cycle;
    uint64_t dropped;
    uint64_t truncations;
    runtime_history_media_change_kind start_kind;
    uint32_t start_arg1;

    runtime_tm_input *inputs;
    uint32_t input_cap;
    uint32_t input_count;
    uint32_t input_head;
    uint32_t replay_i; /* offset from tail; time-travel input cursor */
};

static uint32_t tm_cp_index(const struct runtime_tm_recorder *rec, uint32_t logical)
{
    /* logical 0 = oldest, count-1 = newest */
    return (rec->head + rec->slot_count - rec->count + logical) % rec->slot_count;
}

static runtime_tm_checkpoint *tm_cp_at(
    struct runtime_tm_recorder *rec, uint32_t logical)
{
    return &rec->slots[tm_cp_index(rec, logical)];
}

static void tm_recorder_drop_oldest(struct runtime_tm_recorder *rec)
{
    runtime_tm_checkpoint *cp;

    if (rec == NULL || rec->count == 0u) {
        return;
    }
    cp = tm_cp_at(rec, 0u);
    rec->used -= cp->size;
    free(cp->blob);
    memset(cp, 0, sizeof(*cp));
    rec->count--;
    rec->dropped++;
}

static void tm_input_drop_older_than(struct runtime_tm_recorder *rec, uint64_t cycle)
{
    while (rec->input_count > 0u) {
        uint32_t tail =
            (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
        if (rec->inputs[tail].cycle >= cycle) {
            break;
        }
        rec->input_count--;
    }
}

static void tm_recorder_destroy(struct runtime_tm_recorder *rec)
{
    uint32_t i;

    if (rec == NULL) {
        return;
    }
    if (rec->slots != NULL) {
        for (i = 0u; i < rec->slot_count; ++i) {
            free(rec->slots[i].blob);
        }
        free(rec->slots);
    }
    free(rec->inputs);
    free(rec);
}

void runtime_tm_recorder_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    tm_recorder_destroy(rt->tm_recorder);
    rt->tm_recorder = NULL;
}

static struct runtime_tm_recorder *tm_recorder_ensure(runtime *rt)
{
    struct runtime_tm_recorder *rec;
    uint64_t budget;
    size_t approx_cp;
    uint32_t slots;

    if (rt == NULL) {
        return NULL;
    }
    if (rt->tm_recorder != NULL) {
        return rt->tm_recorder;
    }
    if (rt->timemachine_memory_mb == 0u) {
        return NULL;
    }
    rec = (struct runtime_tm_recorder *)calloc(1u, sizeof(*rec));
    if (rec == NULL) {
        return NULL;
    }
    budget = (uint64_t)rt->timemachine_memory_mb * 1024ull * 1024ull;
    rec->memory_budget = (size_t)budget;
    rec->cadence_cycles = RUNTIME_TM_CHECKPOINT_CADENCE_CYCLES;
    approx_cp = 180u * 1024u;
    slots = (uint32_t)(budget / approx_cp);
    if (slots < 2u) {
        slots = 2u;
    }
    rec->slots = (runtime_tm_checkpoint *)calloc(slots, sizeof(*rec->slots));
    rec->slot_count = slots;
    rec->input_cap = RUNTIME_TM_INPUT_CAP_DEFAULT;
    rec->inputs = (runtime_tm_input *)calloc(rec->input_cap, sizeof(*rec->inputs));
    if (rec->slots == NULL || rec->inputs == NULL) {
        tm_recorder_destroy(rec);
        return NULL;
    }
    rt->tm_recorder = rec;
    return rec;
}

static void tm_log_input(
    void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c)
{
    runtime *rt = (runtime *)user;
    struct runtime_tm_recorder *rec;
    runtime_tm_input *ev;

    if (rt == NULL || rt->tm_recorder == NULL || !rt->tm_recorder->recording) {
        return;
    }
    rec = rt->tm_recorder;
    if (rec->input_count == rec->input_cap) {
        rec->input_count--;
    }
    ev = &rec->inputs[rec->input_head];
    ev->cycle = cycle;
    ev->kind = (uint8_t)kind;
    ev->a = (uint8_t)a;
    ev->b = (uint8_t)b;
    ev->c = (uint8_t)c;
    rec->input_head = (rec->input_head + 1u) % rec->input_cap;
    rec->input_count++;
}

static void tm_on_media(
    void *user, uint64_t cycle, int slot, int device, int kind)
{
    runtime_tm_on_media_event((runtime *)user, cycle, slot, device, kind);
}

bool runtime_tm_checkpoint_take(runtime *rt)
{
    struct runtime_tm_recorder *rec;
    runtime_tm_checkpoint *cp;
    size_t size;
    uint8_t *blob;
    uint64_t cycle;
    runtime_history_status st;

    if (rt == NULL || !rt->machine_ready) {
        return false;
    }
    rec = tm_recorder_ensure(rt);
    if (rec == NULL || !rec->recording) {
        return false;
    }
    cycle = apple2_cycles(&rt->machine);
    size = apple2_snapshot_size(&rt->machine);
    if (size == 0u) {
        return false;
    }
    while (rec->count > 0u &&
           rec->used + size > rec->memory_budget) {
        tm_recorder_drop_oldest(rec);
    }
    if (rec->count == rec->slot_count) {
        tm_recorder_drop_oldest(rec);
    }
    blob = (uint8_t *)malloc(size);
    if (blob == NULL) {
        return false;
    }
    if (apple2_snapshot_save(&rt->machine, blob, size) != size) {
        free(blob);
        return false;
    }
    cp = &rec->slots[rec->head];
    free(cp->blob);
    memset(cp, 0, sizeof(*cp));
    cp->cycle = cycle;
    cp->size = size;
    cp->blob = blob;
    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &st);
        cp->epoch = st.epoch;
    }
    rec->head = (rec->head + 1u) % rec->slot_count;
    rec->count++;
    rec->used += size;
    rec->last_checkpoint_cycle = cycle;
    tm_input_drop_older_than(
        rec, rec->count > 0u ? tm_cp_at(rec, 0u)->cycle : cycle);
    return true;
}

void runtime_tm_recorder_set_enabled(runtime *rt, bool enabled)
{
    struct runtime_tm_recorder *rec;

    if (rt == NULL) {
        return;
    }
    if (!enabled) {
        if (rt->tm_recorder != NULL) {
            rt->tm_recorder->recording = false;
        }
        apple2_set_input_event_callback(&rt->machine, NULL, NULL);
        apple2_set_media_event_callback(&rt->machine, NULL, NULL);
        return;
    }
    rec = tm_recorder_ensure(rt);
    if (rec == NULL) {
        return;
    }
    rec->recording = true;
    apple2_set_input_event_callback(&rt->machine, tm_log_input, rt);
    apple2_set_media_event_callback(&rt->machine, tm_on_media, rt);
    if (rt->machine_ready) {
        (void)runtime_tm_checkpoint_take(rt);
    }
}

void runtime_tm_after_step(runtime *rt)
{
    struct runtime_tm_recorder *rec;
    uint64_t cycle;

    if (rt == NULL || rt->tm_recorder == NULL || !rt->tm_recorder->recording) {
        return;
    }
    rec = rt->tm_recorder;
    cycle = apple2_cycles(&rt->machine);
    if (cycle - rec->last_checkpoint_cycle >= (uint64_t)rec->cadence_cycles) {
        (void)runtime_tm_checkpoint_take(rt);
    }
}

static const runtime_tm_checkpoint *tm_nearest_cp(
    const struct runtime_tm_recorder *rec, uint64_t cycle)
{
    uint32_t i;
    const runtime_tm_checkpoint *best = NULL;

    if (rec == NULL) {
        return NULL;
    }
    for (i = 0u; i < rec->count; ++i) {
        const runtime_tm_checkpoint *cp =
            &rec->slots[tm_cp_index(rec, i)];
        if (cp->cycle <= cycle) {
            best = cp;
        }
    }
    return best;
}

static void tm_apply_input(apple2_t *dst, const runtime_tm_input *ev)
{
    switch (ev->kind) {
    case APPLE2_INPUT_KEY:
        apple2_set_key(dst, ev->a);
        break;
    case APPLE2_INPUT_GAMEPORT_AXIS:
        apple2_gameport_set_axis(dst, (int)ev->a, ev->b);
        break;
    case APPLE2_INPUT_GAMEPORT_BUTTONS:
        apple2_gameport_set_buttons(dst, ev->a);
        break;
    default:
        break;
    }
}

static void tm_replay_inputs(
    struct runtime_tm_recorder *rec,
    apple2_t *dst,
    uint64_t from_cycle,
    uint64_t to_cycle)
{
    uint32_t i;
    uint32_t tail;

    if (rec->input_count == 0u) {
        return;
    }
    tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    for (i = 0u; i < rec->input_count; ++i) {
        const runtime_tm_input *ev =
            &rec->inputs[(tail + i) % rec->input_cap];
        if (ev->cycle < from_cycle) {
            continue;
        }
        if (ev->cycle > to_cycle) {
            break;
        }
        tm_apply_input(dst, ev);
    }
}

bool runtime_tm_materialize(runtime *rt, uint64_t cycle, apple2_t *dst)
{
    struct runtime_tm_recorder *rec;
    const runtime_tm_checkpoint *cp;
    uint64_t hst1_before = 0;
    uint32_t frames_before = 0;
    runtime_history_status st;
    runtime_frame_ring_info fi;
    runtime_tm_window window;

    if (rt == NULL || dst == NULL) {
        return false;
    }
    rec = rt->tm_recorder;
    if (rec == NULL || rec->count == 0u) {
        return false;
    }
    runtime_tm_window_info(rt, &window);
    if (!window.valid || cycle < window.oldest_cycle || cycle > window.newest_cycle) {
        return false;
    }
    cp = tm_nearest_cp(rec, cycle);
    if (cp == NULL || cp->blob == NULL) {
        return false;
    }
    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &st);
        hst1_before = st.record_count;
    }
    runtime_frame_ring_get_info(&rt->frame_ring, &fi);
    frames_before = fi.count;

    if (!apple2_snapshot_load(dst, cp->blob, cp->size)) {
        return false;
    }
    /* Load rebuilds banking. Reseed beam from Φ0; paint as the beam runs. */
    apple2_video_reseed_from_cycles(dst);
    dst->video.paint_enabled = true;
    apple2_set_replay_sealed(dst, true);
    apple2_set_cpu_observer(dst, NULL, NULL);
    apple2_set_memory_access_callback(dst, NULL, NULL);

    tm_replay_inputs(rec, dst, cp->cycle, cycle);
    while (apple2_cycles(dst) < cycle) {
        uint64_t remain = cycle - apple2_cycles(dst);
        uint32_t ran = 0u;
        uint32_t step = remain > 4096u ? 4096u : (uint32_t)remain;
        if (runtime_quit_requested(rt)) {
            apple2_set_replay_sealed(dst, false);
            return false;
        }
        if (!apple2_step_cycles(dst, step, &ran) || ran == 0u) {
            break;
        }
    }
    apple2_set_replay_sealed(dst, false);

    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &st);
        if (st.record_count != hst1_before) {
            return false;
        }
    }
    runtime_frame_ring_get_info(&rt->frame_ring, &fi);
    if (fi.count != frames_before) {
        return false;
    }
    return apple2_cycles(dst) >= cp->cycle;
}

static void tm_truncate_to_cycle(
    runtime *rt,
    uint64_t cycle,
    uint64_t marker_id,
    runtime_history_media_change_kind kind,
    uint32_t arg1)
{
    struct runtime_tm_recorder *rec = rt->tm_recorder;
    uint32_t kept = 0u;
    uint32_t i;

    if (rt->history != NULL && marker_id != 0u) {
        runtime_history_status st;
        runtime_history_get_status(rt->history, &st);
        (void)runtime_history_retain_from(rt->history, st.epoch, marker_id);
    }
    runtime_frame_ring_drop_older_than(&rt->frame_ring, cycle);
    if (rec == NULL) {
        return;
    }
    rec->start_kind = kind;
    rec->start_arg1 = arg1;
    rec->truncations++;
    for (i = 0u; i < rec->count; ++i) {
        runtime_tm_checkpoint *cp = tm_cp_at(rec, i);
        if (cp->cycle < cycle) {
            rec->used -= cp->size;
            free(cp->blob);
            memset(cp, 0, sizeof(*cp));
        } else {
            kept++;
        }
    }
    rec->dropped += (uint64_t)(rec->count - kept);
    rec->count = kept;
    /* Compact surviving slots to the front of the ring. */
    if (kept > 0u) {
        runtime_tm_checkpoint *tmp =
            (runtime_tm_checkpoint *)calloc(rec->slot_count, sizeof(*tmp));
        uint32_t w = 0u;
        if (tmp != NULL) {
            for (i = 0u; i < rec->slot_count; ++i) {
                if (rec->slots[i].blob != NULL) {
                    tmp[w++] = rec->slots[i];
                }
            }
            free(rec->slots);
            rec->slots = tmp;
            rec->head = w % rec->slot_count;
            rec->count = w;
        }
    } else {
        rec->head = 0u;
        rec->used = 0u;
    }
    tm_input_drop_older_than(rec, cycle);
}

void runtime_tm_on_media_event(
    runtime *rt, uint64_t cycle, int slot, int device, int kind)
{
    uint64_t marker_id = 0u;
    runtime_history_record rec;
    runtime_history_media_change_kind mk;

    if (rt == NULL || !rt->timemachine_enabled) {
        return;
    }
    if (rt->tm_recorder != NULL && !rt->tm_recorder->recording) {
        return;
    }
    mk = (kind == APPLE2_MEDIA_EVENT_HOST_DIRECTORY) ?
        RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY :
        RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE;
    if (rt->history != NULL) {
        if (runtime_history_append_marker(
                rt->history,
                RUNTIME_HISTORY_MARKER_MEDIA_CHANGED,
                (uint32_t)mk,
                (uint32_t)(((slot & 0xff) << 8) | (device & 0xff)),
                cycle) &&
            runtime_history_last(rt->history, &rec)) {
            marker_id = rec.id;
        }
    }
    tm_truncate_to_cycle(
        rt,
        cycle,
        marker_id,
        mk,
        (uint32_t)(((slot & 0xff) << 8) | (device & 0xff)));
    if (rt->tm_recorder != NULL && rt->tm_recorder->recording &&
        rt->tm_recorder->count == 0u) {
        (void)runtime_tm_checkpoint_take(rt);
    }
}

void runtime_tm_on_history_resume(runtime *rt)
{
    runtime_history_record rec;
    runtime_history_record found;
    bool have = false;

    if (rt == NULL || rt->history == NULL || !rt->timemachine_enabled) {
        return;
    }
    if (!runtime_history_last(rt->history, &rec)) {
        return;
    }
    found = rec;
    for (;;) {
        if (rec.kind == RUNTIME_HISTORY_RECORD_MARKER &&
            rec.marker_kind == RUNTIME_HISTORY_MARKER_RECORDER_RESUME) {
            found = rec;
            have = true;
            break;
        }
        if (!runtime_history_previous(rt->history, rec.epoch, rec.id, &rec)) {
            break;
        }
    }
    if (!have) {
        return;
    }
    tm_truncate_to_cycle(
        rt,
        found.machine_cycle,
        found.id,
        RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN,
        0u);
}

void runtime_tm_on_history_invalidate(runtime *rt)
{
    struct runtime_tm_recorder *rec;
    uint32_t i;

    if (rt == NULL || rt->tm_recorder == NULL) {
        return;
    }
    rec = rt->tm_recorder;
    for (i = 0u; i < rec->slot_count; ++i) {
        free(rec->slots[i].blob);
        memset(&rec->slots[i], 0, sizeof(rec->slots[i]));
    }
    rec->count = 0u;
    rec->head = 0u;
    rec->used = 0u;
    rec->last_checkpoint_cycle = 0u;
    rec->input_count = 0u;
    rec->input_head = 0u;
    rec->start_kind = RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN;
    rec->start_arg1 = 0u;
    if (rec->recording && rt->machine_ready) {
        (void)runtime_tm_checkpoint_take(rt);
    }
}

void runtime_tm_checkpoint_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *newest, uint64_t *count)
{
    struct runtime_tm_recorder *rec;

    if (oldest != NULL) {
        *oldest = 0u;
    }
    if (newest != NULL) {
        *newest = 0u;
    }
    if (count != NULL) {
        *count = 0u;
    }
    if (rt == NULL || rt->tm_recorder == NULL || rt->tm_recorder->count == 0u) {
        return;
    }
    rec = rt->tm_recorder;
    if (oldest != NULL) {
        *oldest = tm_cp_at(rec, 0u)->cycle;
    }
    if (newest != NULL) {
        *newest = tm_cp_at(rec, rec->count - 1u)->cycle;
    }
    if (count != NULL) {
        *count = rec->count;
    }
}

uint64_t runtime_tm_checkpoint_count(const runtime *rt)
{
    return (rt != NULL && rt->tm_recorder != NULL) ? rt->tm_recorder->count : 0u;
}

uint64_t runtime_tm_checkpoints_dropped(const runtime *rt)
{
    return (rt != NULL && rt->tm_recorder != NULL) ? rt->tm_recorder->dropped : 0u;
}

uint64_t runtime_tm_media_truncations(const runtime *rt)
{
    return (rt != NULL && rt->tm_recorder != NULL) ?
        rt->tm_recorder->truncations : 0u;
}

bool runtime_tm_recorder_is_recording(const runtime *rt)
{
    return rt != NULL && rt->tm_recorder != NULL && rt->tm_recorder->recording;
}

void runtime_tm_fill_window_extras(const runtime *rt, runtime_tm_window *out)
{
    if (rt == NULL || out == NULL || rt->tm_recorder == NULL) {
        return;
    }
    out->checkpoint_count = rt->tm_recorder->count;
    out->checkpoints_dropped = rt->tm_recorder->dropped;
    out->media_truncations = rt->tm_recorder->truncations;
    out->start_kind = rt->tm_recorder->start_kind;
    out->start_arg1 = rt->tm_recorder->start_arg1;
}

void runtime_tm_apply_logged_inputs(
    runtime *rt, apple2_t *dst, uint64_t from_inclusive, uint64_t to_inclusive)
{
    struct runtime_tm_recorder *rec;
    uint32_t tail;

    if (rt == NULL || dst == NULL || rt->tm_recorder == NULL) {
        return;
    }
    rec = rt->tm_recorder;
    if (to_inclusive < from_inclusive || rec->input_count == 0u) {
        return;
    }
    tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    if (rec->replay_i > rec->input_count) {
        rec->replay_i = 0u;
    }
    while (rec->replay_i < rec->input_count) {
        const runtime_tm_input *ev =
            &rec->inputs[(tail + rec->replay_i) % rec->input_cap];
        if (ev->cycle < from_inclusive) {
            rec->replay_i++;
            continue;
        }
        if (ev->cycle > to_inclusive) {
            break;
        }
        tm_apply_input(dst, ev);
        rec->replay_i++;
    }
}

bool runtime_tm_load_nearest_checkpoint(runtime *rt, uint64_t cycle)
{
    struct runtime_tm_recorder *rec;
    const runtime_tm_checkpoint *cp;

    if (rt == NULL || !rt->machine_ready || rt->tm_recorder == NULL) {
        return false;
    }
    rec = rt->tm_recorder;
    cp = tm_nearest_cp(rec, cycle);
    if (cp == NULL || cp->blob == NULL) {
        return false;
    }
    if (!apple2_snapshot_load(&rt->machine, cp->blob, cp->size)) {
        return false;
    }
    apple2_video_reseed_from_cycles(&rt->machine);
    rt->machine.video.paint_enabled = true;
    apple2_set_replay_sealed(&rt->machine, true);
    apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    apple2_set_memory_access_callback(&rt->machine, NULL, NULL);
    rec->replay_i = 0u;
    return true;
}

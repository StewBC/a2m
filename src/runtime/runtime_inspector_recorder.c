#include "runtime_inspector.h"

#include "apple2.h"
#include "apple2_snapshot.h"
#include "runtime_frame_ring.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "video.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum { RUNTIME_INSPECTOR_INPUT_CAP_DEFAULT = 65536u };

typedef enum runtime_inspector_replay_event_kind {
    RUNTIME_INSPECTOR_REPLAY_INPUT = 1,
    RUNTIME_INSPECTOR_REPLAY_ENTER_MAX,
    RUNTIME_INSPECTOR_REPLAY_MAX_BLOCK_PAINT,
    RUNTIME_INSPECTOR_REPLAY_LEAVE_MAX
} runtime_inspector_replay_event_kind;

typedef struct runtime_inspector_sample {
    runtime_inspector_sample_meta meta;
    size_t size;
    uint8_t *blob;
} runtime_inspector_sample;

typedef struct runtime_inspector_anchor {
    bool valid;
    uint64_t timeline_generation;
    uint64_t snapshot_cycle;
    uint64_t snapshot_replay_watermark;
    uint8_t execution_mode;
    size_t size;
    uint8_t *blob;
    uint32_t *resume_framebuffer;
} runtime_inspector_anchor;

typedef struct runtime_inspector_pending_frame {
    bool valid;
    uint64_t sample_id;
    uint64_t timeline_generation;
    uint64_t frame_cycle;
    uint64_t frame_number;
    uint64_t picture_id;
    uint64_t frame_replay_watermark;
    uint32_t *pixels;
} runtime_inspector_pending_frame;

typedef struct runtime_inspector_input {
    uint64_t sequence;
    uint64_t boundary_cycle;
    uint8_t event_kind;
    uint8_t kind;
    uint8_t a;
    uint8_t b;
    uint8_t c;
} runtime_inspector_input;

struct runtime_inspector_recorder {
    bool recording;
    bool anchor_pending;
    size_t memory_budget;
    size_t used;
    uint32_t slot_count;
    uint32_t count;
    uint32_t head;
    runtime_inspector_sample *slots;
    runtime_inspector_anchor anchor;
    runtime_inspector_pending_frame pending;
    uint64_t next_sample_id;
    uint64_t timeline_generation;
    uint64_t next_replay_sequence;
    uint64_t dropped;
    uint64_t truncations;
    runtime_history_media_change_kind start_kind;
    uint32_t start_arg1;
    uint64_t media_write_generation;
    uint64_t media_generation_seen_at_last_cadence;
    uint64_t media_flush_attempt_generation;
    uint64_t media_quiet_candidate_generation;
    bool waiting_for_media_anchor;
    bool media_anchor_pending;
    runtime_inspector_input *inputs;
    uint32_t *work_framebuffer;
    uint32_t input_cap;
    uint32_t input_count;
    uint32_t input_head;
    uint32_t replay_i;
};

static const size_t inspector_framebuffer_bytes =
    (size_t)RUNTIME_FRAME_RING_PIXELS * sizeof(uint32_t);

void runtime_inspector_catalog_destroy(runtime_inspector_catalog *catalog)
{
    if (catalog == NULL) return;
    free(catalog->samples);
    memset(catalog, 0, sizeof(*catalog));
}

static uint32_t inspector_sample_index(
    const struct runtime_inspector_recorder *rec,
    uint32_t logical)
{
    return (rec->head + rec->slot_count - rec->count + logical) % rec->slot_count;
}

static runtime_inspector_sample *inspector_sample_at(
    struct runtime_inspector_recorder *rec,
    uint32_t logical)
{
    return &rec->slots[inspector_sample_index(rec, logical)];
}

static const runtime_inspector_sample *inspector_sample_at_const(
    const struct runtime_inspector_recorder *rec,
    uint32_t logical)
{
    return &rec->slots[inspector_sample_index(rec, logical)];
}

void runtime_inspector_publish_catalog(runtime *rt)
{
    runtime_inspector_catalog_slot *slot;
    struct runtime_inspector_recorder *rec;
    runtime_inspector_sample_meta *grown;
    uint32_t i;
    uint64_t visible_count;

    if (rt == NULL || rt->inspector_catalog_slot.mutex == NULL) return;
    slot = &rt->inspector_catalog_slot;
    rec = rt->inspector_recorder;
    mutex_lock(slot->mutex);
    if (rec == NULL) {
        slot->count = 0u;
        slot->timeline_generation = 0u;
        mutex_unlock(slot->mutex);
        return;
    }
    visible_count = rec->count;
    if (rt->inspecting && rt->inspector_now_valid &&
        !rt->inspector_now_aliases_sample) {
        visible_count++;
    }
    if (slot->capacity < visible_count) {
        grown = (runtime_inspector_sample_meta *)realloc(
            slot->samples, (size_t)visible_count * sizeof(*grown));
        if (grown == NULL) {
            mutex_unlock(slot->mutex);
            return;
        }
        slot->samples = grown;
        slot->capacity = (size_t)visible_count;
    }
    for (i = 0u; i < rec->count; i++) {
        slot->samples[i] = inspector_sample_at_const(rec, i)->meta;
        slot->samples[i].picture_available = runtime_frame_ring_has_picture_id(
            &rt->frame_ring, slot->samples[i].picture_id) ? 1u : 0u;
    }
    if (visible_count > rec->count) {
        runtime_inspector_sample_meta *now = &slot->samples[rec->count];
        memset(now, 0, sizeof(*now));
        now->sample_id = rt->inspector_now_endpoint_id;
        now->timeline_generation = rt->inspector_now_timeline_generation;
        now->frame_cycle = rt->inspector_now_cycle;
        now->snapshot_cycle = rt->inspector_now_cycle;
        now->picture_id = rt->inspector_now_endpoint_id;
        now->execution_mode = rt->inspector_now_execution_mode;
        now->kind = RUNTIME_INSPECTOR_SAMPLE_NOW;
        now->picture_available = runtime_inspector_now_picture_available(
            rt, now->picture_id) ? 1u : 0u;
    }
    slot->count = visible_count;
    slot->timeline_generation = rec->timeline_generation;
    mutex_unlock(slot->mutex);
}

static void inspector_sample_free(runtime_inspector_sample *sample)
{
    if (sample == NULL) {
        return;
    }
    free(sample->blob);
    memset(sample, 0, sizeof(*sample));
}

static void inspector_anchor_free(runtime_inspector_anchor *anchor)
{
    if (anchor == NULL) {
        return;
    }
    free(anchor->blob);
    free(anchor->resume_framebuffer);
    memset(anchor, 0, sizeof(*anchor));
}

static size_t inspector_sample_bytes(const runtime_inspector_sample *sample)
{
    return sample == NULL ? 0u : sample->size;
}

static size_t inspector_anchor_bytes(const runtime_inspector_anchor *anchor)
{
    return anchor == NULL || !anchor->valid ? 0u :
        anchor->size + inspector_framebuffer_bytes;
}

static void inspector_pending_clear(runtime_inspector_pending_frame *pending)
{
    uint32_t *pixels;
    if (pending == NULL) {
        return;
    }
    pixels = pending->pixels;
    memset(pending, 0, sizeof(*pending));
    pending->pixels = pixels;
}

static void inspector_inputs_clear(struct runtime_inspector_recorder *rec)
{
    rec->input_count = 0u;
    rec->input_head = 0u;
    rec->replay_i = 0u;
    rec->next_replay_sequence = 0u;
}

static void inspector_inputs_drop_through(
    struct runtime_inspector_recorder *rec, uint64_t watermark)
{
    while (rec != NULL && rec->input_count > 0u) {
        uint32_t tail =
            (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
        if (rec->inputs[tail].sequence > watermark) break;
        rec->input_count--;
    }
    if (rec != NULL) rec->replay_i = 0u;
}

static void inspector_window_clear(runtime *rt, bool advance_generation)
{
    struct runtime_inspector_recorder *rec;
    uint32_t i;

    if (rt == NULL || rt->inspector_recorder == NULL) {
        return;
    }
    rec = rt->inspector_recorder;
    for (i = 0u; i < rec->slot_count; i++) {
        inspector_sample_free(&rec->slots[i]);
    }
    inspector_anchor_free(&rec->anchor);
    inspector_pending_clear(&rec->pending);
    rec->count = 0u;
    rec->head = 0u;
    rec->used = 0u;
    rec->anchor_pending = false;
    inspector_inputs_clear(rec);
    if (advance_generation) {
        rec->timeline_generation++;
        if (rec->timeline_generation == 0u) {
            rec->timeline_generation = 1u;
        }
    }
    runtime_frame_ring_clear(&rt->frame_ring);
    runtime_inspector_publish_catalog(rt);
}

static bool inspector_media_cadence_ready(
    struct runtime_inspector_recorder *rec)
{
    if (!rec->waiting_for_media_anchor) return true;
    if (rec->media_write_generation !=
        rec->media_generation_seen_at_last_cadence) {
        rec->media_generation_seen_at_last_cadence = rec->media_write_generation;
        rec->media_anchor_pending = false;
        return false;
    }
    rec->media_quiet_candidate_generation = rec->media_write_generation;
    rec->media_flush_attempt_generation = 0u;
    rec->media_anchor_pending = true;
    return false;
}

static void inspector_recorder_free(struct runtime_inspector_recorder *rec)
{
    uint32_t i;
    if (rec == NULL) {
        return;
    }
    if (rec->slots != NULL) {
        for (i = 0u; i < rec->slot_count; i++) {
            inspector_sample_free(&rec->slots[i]);
        }
    }
    inspector_anchor_free(&rec->anchor);
    free(rec->pending.pixels);
    free(rec->work_framebuffer);
    free(rec->slots);
    free(rec->inputs);
    free(rec);
}

void runtime_inspector_recorder_destroy(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    inspector_recorder_free(rt->inspector_recorder);
    rt->inspector_recorder = NULL;
}

static struct runtime_inspector_recorder *inspector_recorder_ensure(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    uint64_t budget;
    size_t approx_sample;
    uint32_t slots;

    if (rt == NULL) {
        return NULL;
    }
    if (rt->inspector_recorder != NULL) {
        return rt->inspector_recorder;
    }
    if (rt->inspector_memory_mb == 0u) {
        return NULL;
    }
    rec = (struct runtime_inspector_recorder *)calloc(1u, sizeof(*rec));
    if (rec == NULL) {
        return NULL;
    }
    budget = (uint64_t)rt->inspector_memory_mb * 1024ull * 1024ull;
    rec->memory_budget = (size_t)budget;
    approx_sample = 180u * 1024u;
    slots = (uint32_t)(budget / approx_sample);
    if (slots < 2u) {
        slots = 2u;
    }
    rec->slots = (runtime_inspector_sample *)calloc(slots, sizeof(*rec->slots));
    rec->pending.pixels = (uint32_t *)malloc(inspector_framebuffer_bytes);
    rec->work_framebuffer = (uint32_t *)malloc(inspector_framebuffer_bytes);
    rec->input_cap = RUNTIME_INSPECTOR_INPUT_CAP_DEFAULT;
    rec->inputs = (runtime_inspector_input *)calloc(rec->input_cap, sizeof(*rec->inputs));
    if (rec->slots == NULL || rec->pending.pixels == NULL ||
        rec->work_framebuffer == NULL || rec->inputs == NULL) {
        inspector_recorder_free(rec);
        return NULL;
    }
    rec->slot_count = slots;
    rec->next_sample_id = 1u;
    rec->timeline_generation = 1u;
    rt->inspector_recorder = rec;
    return rec;
}

static bool inspector_snapshot_copy(
    runtime *rt,
    uint8_t **out_blob,
    size_t *out_size,
    uint32_t **out_framebuffer)
{
    size_t size;
    uint8_t *blob;
    uint32_t *fb;

    if (rt == NULL || out_blob == NULL || out_size == NULL ||
        !rt->machine_ready ||
        rt->machine.video.fb == NULL) {
        return false;
    }
    size = apple2_snapshot_size(&rt->machine);
    if (size == 0u) {
        return false;
    }
    blob = (uint8_t *)malloc(size);
    fb = out_framebuffer == NULL ? NULL :
        (uint32_t *)malloc(inspector_framebuffer_bytes);
    if (blob == NULL || (out_framebuffer != NULL && fb == NULL)) {
        free(blob);
        free(fb);
        return false;
    }
    if (apple2_snapshot_save(&rt->machine, blob, size) != size) {
        free(blob);
        free(fb);
        return false;
    }
    if (fb != NULL) {
        memcpy(fb, rt->machine.video.fb, inspector_framebuffer_bytes);
    }
    *out_blob = blob;
    *out_size = size;
    if (out_framebuffer != NULL) {
        *out_framebuffer = fb;
    }
    return true;
}

static uint8_t inspector_live_execution_mode(const runtime *rt)
{
    return runtime_turbo_is_max_value(rt->active_turbo_multiplier) ?
        RUNTIME_INSPECTOR_EXECUTION_MAX : RUNTIME_INSPECTOR_EXECUTION_FINITE;
}

static bool inspector_take_anchor(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    runtime_inspector_anchor anchor;

    if (rt == NULL || !rt->machine_ready || rt->inspector_recorder == NULL) {
        return false;
    }
    rec = rt->inspector_recorder;
    memset(&anchor, 0, sizeof(anchor));
    if (!inspector_snapshot_copy(
            rt, &anchor.blob, &anchor.size, &anchor.resume_framebuffer)) {
        return false;
    }
    anchor.valid = true;
    anchor.timeline_generation = rec->timeline_generation;
    anchor.snapshot_cycle = apple2_cycles(&rt->machine);
    anchor.snapshot_replay_watermark = rec->next_replay_sequence;
    anchor.execution_mode = inspector_live_execution_mode(rt);
    if (anchor.size + inspector_framebuffer_bytes > rec->memory_budget) {
        inspector_anchor_free(&anchor);
        return false;
    }
    inspector_anchor_free(&rec->anchor);
    rec->anchor = anchor;
    rec->used = inspector_anchor_bytes(&rec->anchor);
    rec->anchor_pending = false;
    return true;
}

bool runtime_inspector_checkpoint_take(runtime *rt)
{
    struct runtime_inspector_recorder *rec = inspector_recorder_ensure(rt);
    if (rec == NULL || !rec->recording) {
        return false;
    }
    if (rec->anchor.valid) {
        return true;
    }
    if (rt->machine.cpu.micro_active) {
        rec->anchor_pending = true;
        return true;
    }
    return inspector_take_anchor(rt);
}

static void inspector_log_input(
    void *user, uint64_t cycle, int kind, uint32_t a, uint32_t b, uint32_t c)
{
    runtime *rt = (runtime *)user;
    struct runtime_inspector_recorder *rec;
    runtime_inspector_input *ev;

    if (rt == NULL || rt->inspector_recorder == NULL ||
        !rt->inspector_recorder->recording) {
        return;
    }
    rec = rt->inspector_recorder;
    if (rec->input_count == rec->input_cap) {
        inspector_window_clear(rt, true);
        rec->anchor_pending = true;
        return;
    }
    ev = &rec->inputs[rec->input_head];
    ev->sequence = ++rec->next_replay_sequence;
    ev->boundary_cycle = cycle;
    ev->event_kind = RUNTIME_INSPECTOR_REPLAY_INPUT;
    ev->kind = (uint8_t)kind;
    ev->a = (uint8_t)a;
    ev->b = (uint8_t)b;
    ev->c = (uint8_t)c;
    rec->input_head = (rec->input_head + 1u) % rec->input_cap;
    rec->input_count++;
}

static bool inspector_append_barrier(
    runtime *rt,
    runtime_inspector_replay_event_kind kind)
{
    struct runtime_inspector_recorder *rec;
    runtime_inspector_input *ev;
    if (rt == NULL || rt->inspector_recorder == NULL) return false;
    rec = rt->inspector_recorder;
    if (!rec->recording) return false;
    if (rec->input_count == rec->input_cap) {
        inspector_window_clear(rt, true);
        if (rt->machine.cpu.micro_active || !inspector_take_anchor(rt)) {
            rec->anchor_pending = true;
            return false;
        }
    }
    ev = &rec->inputs[rec->input_head];
    memset(ev, 0, sizeof(*ev));
    ev->sequence = ++rec->next_replay_sequence;
    ev->boundary_cycle = apple2_cycles(&rt->machine);
    ev->event_kind = (uint8_t)kind;
    rec->input_head = (rec->input_head + 1u) % rec->input_cap;
    rec->input_count++;
    return true;
}

void runtime_inspector_on_execution_mode_transition(
    runtime *rt,
    bool entering_max,
    bool leaving_max)
{
    if (entering_max) {
        (void)inspector_append_barrier(rt, RUNTIME_INSPECTOR_REPLAY_ENTER_MAX);
    } else if (leaving_max) {
        (void)inspector_append_barrier(rt, RUNTIME_INSPECTOR_REPLAY_LEAVE_MAX);
    }
}

static void inspector_on_media(
    void *user, uint64_t cycle, int slot, int device, int kind)
{
    runtime_inspector_on_media_event((runtime *)user, cycle, slot, device, kind);
}

void runtime_inspector_recorder_set_enabled(runtime *rt, bool enabled)
{
    struct runtime_inspector_recorder *rec;
    if (rt == NULL) {
        return;
    }
    if (!enabled) {
        if (rt->inspector_recorder != NULL) {
            rt->inspector_recorder->recording = false;
        }
        apple2_set_input_event_callback(&rt->machine, NULL, NULL);
        apple2_set_media_event_callback(&rt->machine, NULL, NULL);
        return;
    }
    rec = inspector_recorder_ensure(rt);
    if (rec == NULL) {
        return;
    }
    rec->recording = true;
    apple2_set_input_event_callback(&rt->machine, inspector_log_input, rt);
    apple2_set_media_event_callback(&rt->machine, inspector_on_media, rt);
    if (!rec->anchor.valid && rt->machine_ready) {
        (void)runtime_inspector_checkpoint_take(rt);
    }
}

static bool inspector_replay_from_anchor(
    runtime *rt,
    apple2_t *dst,
    uint64_t target_cycle,
    uint64_t target_watermark,
    uint64_t capture_cycle,
    uint32_t *capture_pixels,
    bool *captured,
    uint32_t *resume_pixels);

static bool inspector_promote_oldest(
    runtime *rt, const uint8_t *live_blob, size_t live_size,
    uint8_t live_execution_mode)
{
    struct runtime_inspector_recorder *rec = rt->inspector_recorder;
    runtime_inspector_sample *sample;
    runtime_inspector_anchor promoted;
    size_t old_sample_bytes;
    bool reconstructed;

    if (rec == NULL || rec->count == 0u) {
        return false;
    }
    sample = inspector_sample_at(rec, 0u);
    old_sample_bytes = inspector_sample_bytes(sample);
    memset(&promoted, 0, sizeof(promoted));
    promoted.resume_framebuffer =
        (uint32_t *)malloc(inspector_framebuffer_bytes);
    if (promoted.resume_framebuffer == NULL) {
        return false;
    }
    reconstructed = inspector_replay_from_anchor(
        rt, &rt->machine, sample->meta.snapshot_cycle,
        sample->meta.snapshot_replay_watermark, 0u, NULL, NULL,
        promoted.resume_framebuffer);
    if (!apple2_snapshot_load(&rt->machine, live_blob, live_size)) {
        free(promoted.resume_framebuffer);
        return false;
    }
    memcpy(
        rt->machine.video.fb, rec->work_framebuffer,
        inspector_framebuffer_bytes);
    rt->machine.video.paint_enabled =
        live_execution_mode == RUNTIME_INSPECTOR_EXECUTION_FINITE;
    apple2_set_replay_sealed(&rt->machine, false);
    apple2_set_input_event_callback(&rt->machine, inspector_log_input, rt);
    apple2_set_media_event_callback(&rt->machine, inspector_on_media, rt);
    runtime_inspector_reattach_live_hooks(rt);
    if (!reconstructed) {
        free(promoted.resume_framebuffer);
        return false;
    }
    promoted.valid = true;
    promoted.timeline_generation = sample->meta.timeline_generation;
    promoted.snapshot_cycle = sample->meta.snapshot_cycle;
    promoted.snapshot_replay_watermark = sample->meta.snapshot_replay_watermark;
    promoted.execution_mode = sample->meta.execution_mode;
    promoted.size = sample->size;
    promoted.blob = sample->blob;
    sample->blob = NULL;
    rec->used -= inspector_anchor_bytes(&rec->anchor);
    rec->used -= old_sample_bytes;
    inspector_anchor_free(&rec->anchor);
    inspector_sample_free(sample);
    rec->anchor = promoted;
    rec->used += inspector_anchor_bytes(&rec->anchor);
    rec->count--;
    rec->dropped++;
    inspector_inputs_drop_through(
        rec, rec->anchor.snapshot_replay_watermark);
    if (rec->count > 0u) {
        runtime_frame_ring_drop_before_picture_id(
            &rt->frame_ring, inspector_sample_at(rec, 0u)->meta.picture_id);
    } else {
        runtime_frame_ring_clear(&rt->frame_ring);
    }
    runtime_inspector_publish_catalog(rt);
    return true;
}

static bool inspector_append_pending_sample(runtime *rt, uint8_t kind)
{
    struct runtime_inspector_recorder *rec = rt->inspector_recorder;
    runtime_inspector_sample sample;
    runtime_inspector_sample *slot;
    size_t sample_bytes;

    if (rec == NULL || !rec->recording || !rec->pending.valid ||
        !rec->anchor.valid) {
        return false;
    }
    memset(&sample, 0, sizeof(sample));
    if (!inspector_snapshot_copy(rt, &sample.blob, &sample.size, NULL)) {
        return false;
    }
    sample.meta.sample_id = rec->pending.sample_id;
    sample.meta.timeline_generation = rec->pending.timeline_generation;
    sample.meta.frame_cycle = rec->pending.frame_cycle;
    sample.meta.snapshot_cycle = apple2_cycles(&rt->machine);
    sample.meta.frame_number = rec->pending.frame_number;
    sample.meta.picture_id = rec->pending.picture_id;
    sample.meta.frame_replay_watermark = rec->pending.frame_replay_watermark;
    sample.meta.snapshot_replay_watermark = rec->next_replay_sequence;
    sample.meta.execution_mode = inspector_live_execution_mode(rt);
    sample.meta.kind = kind;
    sample.meta.picture_available = 0u;
    sample_bytes = inspector_sample_bytes(&sample);
    memcpy(
        rec->work_framebuffer, rt->machine.video.fb,
        inspector_framebuffer_bytes);
    while (rec->count > 0u && rec->used + sample_bytes > rec->memory_budget) {
        if (!inspector_promote_oldest(
                rt, sample.blob, sample.size, sample.meta.execution_mode)) {
            inspector_sample_free(&sample);
            return false;
        }
    }
    if (rec->count == rec->slot_count) {
        if (!inspector_promote_oldest(
                rt, sample.blob, sample.size, sample.meta.execution_mode)) {
            inspector_sample_free(&sample);
            return false;
        }
    }
    if (rec->used + sample_bytes > rec->memory_budget) {
        inspector_sample_free(&sample);
        return false;
    }
    sample.meta.picture_available = runtime_frame_ring_push(
        &rt->frame_ring, sample.meta.picture_id, sample.meta.frame_number,
        sample.meta.frame_cycle, DISPLAY_FRAME_WIDTH, DISPLAY_FRAME_HEIGHT,
        rec->pending.pixels) ? 1u : 0u;
    slot = &rec->slots[rec->head];
    inspector_sample_free(slot);
    *slot = sample;
    rec->head = (rec->head + 1u) % rec->slot_count;
    rec->count++;
    rec->used += sample_bytes;
    inspector_pending_clear(&rec->pending);
    runtime_inspector_publish_catalog(rt);
    return true;
}

uint64_t runtime_inspector_on_finite_cadence_frame(
    runtime *rt, uint64_t frame_cycle, uint64_t frame_number,
    const uint32_t *pixels)
{
    struct runtime_inspector_recorder *rec;
    uint64_t id;
    if (rt == NULL || pixels == NULL || rt->inspector_recorder == NULL) {
        return 0u;
    }
    rec = rt->inspector_recorder;
    if (!rec->recording || !rec->anchor.valid) {
        if (rec->recording) (void)inspector_media_cadence_ready(rec);
        return 0u;
    }
    if (!inspector_media_cadence_ready(rec)) return 0u;
    if (rec->pending.valid) {
#ifndef NDEBUG
        assert(!"finite cadence arrived while a sample was pending");
#endif
        return 0u;
    }
    id = rec->next_sample_id++;
    if (id == 0u) {
        id = rec->next_sample_id++;
    }
    rec->pending.valid = true;
    rec->pending.sample_id = id;
    rec->pending.timeline_generation = rec->timeline_generation;
    rec->pending.frame_cycle = frame_cycle;
    rec->pending.frame_number = frame_number;
    rec->pending.picture_id = id;
    rec->pending.frame_replay_watermark = rec->next_replay_sequence;
    memcpy(rec->pending.pixels, pixels, inspector_framebuffer_bytes);
    return id;
}

uint64_t runtime_inspector_on_max_cadence_frame(
    runtime *rt, uint64_t snapshot_cycle, uint64_t frame_number,
    const uint32_t *pixels)
{
    uint64_t id;
    struct runtime_inspector_recorder *rec;
    if (rt == NULL || pixels == NULL || rt->inspector_recorder == NULL) {
        return 0u;
    }
    rec = rt->inspector_recorder;
    if (!rec->recording || !rec->anchor.valid || rec->pending.valid) {
        if (rec->recording) {
            (void)inspector_media_cadence_ready(rec);
            runtime_inspector_on_instruction_boundary(rt);
        }
        return 0u;
    }
    if (!inspector_media_cadence_ready(rec)) return 0u;
    (void)inspector_append_barrier(
        rt, RUNTIME_INSPECTOR_REPLAY_MAX_BLOCK_PAINT);
    id = rec->next_sample_id++;
    if (id == 0u) {
        id = rec->next_sample_id++;
    }
    rec->pending.valid = true;
    rec->pending.sample_id = id;
    rec->pending.timeline_generation = rec->timeline_generation;
    rec->pending.frame_cycle = snapshot_cycle;
    rec->pending.frame_number = frame_number;
    rec->pending.picture_id = id;
    rec->pending.frame_replay_watermark = rec->next_replay_sequence;
    memcpy(rec->pending.pixels, pixels, inspector_framebuffer_bytes);
    if (!inspector_append_pending_sample(rt, RUNTIME_INSPECTOR_SAMPLE_MAX_FRAME)) {
        inspector_pending_clear(&rec->pending);
        return 0u;
    }
    return id;
}

void runtime_inspector_on_instruction_boundary(runtime *rt)
{
    struct runtime_inspector_recorder *rec;
    if (rt == NULL || rt->inspector_recorder == NULL ||
        rt->machine.cpu.micro_active) {
        return;
    }
    rec = rt->inspector_recorder;
    if (!rec->recording) {
        return;
    }
    if (rec->waiting_for_media_anchor && rec->media_anchor_pending &&
        rec->media_quiet_candidate_generation == rec->media_write_generation &&
        rec->media_flush_attempt_generation != rec->media_write_generation) {
        rec->media_flush_attempt_generation = rec->media_write_generation;
        rec->media_anchor_pending = false;
        apple2_set_media_event_callback(&rt->machine, NULL, NULL);
        if (apple2_snapshot_flush_media(&rt->machine)) {
            inspector_inputs_clear(rec);
            if (inspector_take_anchor(rt)) {
                rec->waiting_for_media_anchor = false;
            }
        }
        apple2_set_media_event_callback(&rt->machine, inspector_on_media, rt);
        return;
    }
    if (rec->anchor_pending || !rec->anchor.valid) {
        (void)inspector_take_anchor(rt);
        return;
    }
    if (rec->pending.valid && !inspector_append_pending_sample(
            rt, RUNTIME_INSPECTOR_SAMPLE_FINITE_FRAME)) {
        inspector_pending_clear(&rec->pending);
    }
}

void runtime_inspector_after_step(runtime *rt)
{
    runtime_inspector_on_instruction_boundary(rt);
}

static void inspector_apply_event(apple2_t *dst, const runtime_inspector_input *ev)
{
    if (ev->event_kind == RUNTIME_INSPECTOR_REPLAY_INPUT) {
        switch (ev->kind) {
        case APPLE2_INPUT_KEY: apple2_set_key(dst, ev->a); break;
        case APPLE2_INPUT_GAMEPORT_AXIS:
            apple2_gameport_set_axis(dst, (int)ev->a, ev->b); break;
        case APPLE2_INPUT_GAMEPORT_BUTTONS:
            apple2_gameport_set_buttons(dst, ev->a); break;
        default: break;
        }
    } else if (ev->event_kind == RUNTIME_INSPECTOR_REPLAY_ENTER_MAX) {
        apple2_video_paint_full_frame(dst);
        dst->video.paint_enabled = false;
    } else if (ev->event_kind == RUNTIME_INSPECTOR_REPLAY_MAX_BLOCK_PAINT) {
        apple2_video_paint_full_frame(dst);
    } else if (ev->event_kind == RUNTIME_INSPECTOR_REPLAY_LEAVE_MAX) {
        while (apple2_video_take_frame_ready(dst)) {
        }
        apple2_video_reseed_from_cycles(dst);
        while (apple2_video_take_frame_ready(dst)) {
        }
        dst->video.paint_enabled = true;
    }
}

static const runtime_inspector_sample *inspector_nearest_sample(
    const struct runtime_inspector_recorder *rec, uint64_t cycle)
{
    uint32_t i;
    const runtime_inspector_sample *best = NULL;
    for (i = 0u; rec != NULL && i < rec->count; i++) {
        const runtime_inspector_sample *sample = inspector_sample_at_const(rec, i);
        if (sample->meta.snapshot_cycle <= cycle) {
            best = sample;
        } else {
            break;
        }
    }
    return best;
}

static void inspector_replay_cursor_reset(
    struct runtime_inspector_recorder *rec, uint64_t watermark)
{
    uint32_t i;
    uint32_t tail;
    rec->replay_i = 0u;
    tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    for (i = 0u; i < rec->input_count; i++) {
        const runtime_inspector_input *ev = &rec->inputs[(tail + i) % rec->input_cap];
        if (ev->sequence > watermark) {
            break;
        }
        rec->replay_i++;
    }
}

static void inspector_apply_inputs_before_next_cycle(
    struct runtime_inspector_recorder *rec, apple2_t *dst,
    uint64_t target_watermark)
{
    uint32_t tail =
        (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    while (rec->replay_i < rec->input_count) {
        const runtime_inspector_input *ev =
            &rec->inputs[(tail + rec->replay_i) % rec->input_cap];
        if (ev->sequence > target_watermark ||
            ev->boundary_cycle > apple2_cycles(dst)) {
            break;
        }
        inspector_apply_event(dst, ev);
        rec->replay_i++;
    }
}

static bool inspector_replay_from_anchor(
    runtime *rt,
    apple2_t *dst,
    uint64_t target_cycle,
    uint64_t target_watermark,
    uint64_t capture_cycle,
    uint32_t *capture_pixels,
    bool *captured,
    uint32_t *resume_pixels)
{
    struct runtime_inspector_recorder *rec;

    if (captured != NULL) *captured = false;
    if (rt == NULL || dst == NULL || rt->inspector_recorder == NULL) {
        return false;
    }
    rec = rt->inspector_recorder;
    if (!rec->anchor.valid || rec->anchor.blob == NULL ||
        rec->anchor.resume_framebuffer == NULL ||
        target_cycle < rec->anchor.snapshot_cycle ||
        !apple2_snapshot_load(dst, rec->anchor.blob, rec->anchor.size)) {
        return false;
    }
    memcpy(
        dst->video.fb, rec->anchor.resume_framebuffer,
        inspector_framebuffer_bytes);
    dst->video.paint_enabled =
        rec->anchor.execution_mode == RUNTIME_INSPECTOR_EXECUTION_FINITE;
    apple2_set_replay_sealed(dst, true);
    apple2_set_cpu_observer(dst, NULL, NULL);
    apple2_set_memory_access_callback(dst, NULL, NULL);
    inspector_replay_cursor_reset(rec, rec->anchor.snapshot_replay_watermark);
    (void)apple2_video_take_frame_ready(dst);
    while (apple2_cycles(dst) < target_cycle) {
        inspector_apply_inputs_before_next_cycle(rec, dst, target_watermark);
        if (runtime_quit_requested(rt) ||
            (dst->video.paint_enabled ?
                !apple2_step_cycle(dst) :
                apple2_step_instruction_max(dst) == 0u)) {
            break;
        }
        if (apple2_video_take_frame_ready(dst) && capture_pixels != NULL &&
            apple2_cycles(dst) == capture_cycle) {
            memcpy(capture_pixels, dst->video.fb, inspector_framebuffer_bytes);
            if (captured != NULL) *captured = true;
        }
    }
    if (apple2_cycles(dst) == target_cycle) {
        inspector_apply_inputs_before_next_cycle(rec, dst, target_watermark);
    }
    if (resume_pixels != NULL && apple2_cycles(dst) >= target_cycle) {
        memcpy(resume_pixels, dst->video.fb, inspector_framebuffer_bytes);
    }
    return apple2_cycles(dst) >= target_cycle;
}

bool runtime_inspector_materialize(runtime *rt, uint64_t cycle, apple2_t *dst)
{
    struct runtime_inspector_recorder *rec;
    if (rt == NULL || dst == NULL || rt->inspector_recorder == NULL) {
        return false;
    }
    rec = rt->inspector_recorder;
    if (!rec->anchor.valid || cycle < rec->anchor.snapshot_cycle) {
        return false;
    }
    if (!inspector_replay_from_anchor(
            rt, dst, cycle, rec->next_replay_sequence,
            0u, NULL, NULL, NULL)) return false;
    apple2_set_replay_sealed(dst, false);
    return apple2_cycles(dst) >= cycle;
}

void runtime_inspector_on_media_event(
    runtime *rt, uint64_t cycle, int slot, int device, int kind)
{
    struct runtime_inspector_recorder *rec;
    runtime_history_media_change_kind mk;
    (void)cycle;
    if (rt == NULL || !rt->inspector_enabled || rt->inspector_recorder == NULL ||
        !rt->inspector_recorder->recording) {
        return;
    }
    rec = rt->inspector_recorder;
    mk = kind == APPLE2_MEDIA_EVENT_HOST_DIRECTORY ?
        RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY :
        RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE;
    rec->start_kind = mk;
    rec->start_arg1 = (uint32_t)(((slot & 0xff) << 8) | (device & 0xff));
    rec->truncations++;
    if (rt->history != NULL) {
        (void)runtime_history_append_marker(
            rt->history,
            RUNTIME_HISTORY_MARKER_MEDIA_CHANGED,
            (uint32_t)mk,
            rec->start_arg1,
            cycle);
    }
    if (!rec->waiting_for_media_anchor) {
        inspector_window_clear(rt, true);
        rec->waiting_for_media_anchor = true;
    }
    rec->media_write_generation++;
    if (rec->media_write_generation == 0u) rec->media_write_generation = 1u;
    rec->media_anchor_pending = false;
}

void runtime_inspector_on_history_resume(runtime *rt)
{
    if (rt == NULL || rt->inspector_recorder == NULL || !rt->inspector_enabled) {
        return;
    }
    inspector_window_clear(rt, true);
    rt->inspector_recorder->waiting_for_media_anchor = false;
    rt->inspector_recorder->anchor_pending = true;
}

void runtime_inspector_on_history_invalidate(runtime *rt)
{
    if (rt == NULL || rt->inspector_recorder == NULL) {
        return;
    }
    inspector_window_clear(rt, true);
    rt->inspector_recorder->waiting_for_media_anchor = false;
    if (rt->inspector_recorder->recording && rt->machine_ready) {
        if (rt->machine.cpu.micro_active) {
            rt->inspector_recorder->anchor_pending = true;
        } else {
            (void)inspector_take_anchor(rt);
        }
    }
}

void runtime_inspector_checkpoint_bounds(
    const runtime *rt, uint64_t *oldest, uint64_t *newest, uint64_t *count)
{
    const struct runtime_inspector_recorder *rec =
        rt == NULL ? NULL : rt->inspector_recorder;
    if (oldest != NULL) *oldest = 0u;
    if (newest != NULL) *newest = 0u;
    if (count != NULL) *count = 0u;
    if (rec == NULL || rec->count == 0u) return;
    if (oldest != NULL)
        *oldest = inspector_sample_at_const(rec, 0u)->meta.snapshot_cycle;
    if (newest != NULL)
        *newest = inspector_sample_at_const(rec, rec->count - 1u)->meta.snapshot_cycle;
    if (count != NULL) *count = rec->count;
}

uint64_t runtime_inspector_checkpoint_count(const runtime *rt)
{
    return runtime_inspector_sample_count(rt);
}

uint64_t runtime_inspector_sample_count(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL ?
        rt->inspector_recorder->count : 0u;
}

uint64_t runtime_inspector_timeline_generation(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL ?
        rt->inspector_recorder->timeline_generation : 0u;
}

bool runtime_inspector_sample_meta_at(
    const runtime *rt, uint64_t ordinal, runtime_inspector_sample_meta *out)
{
    const struct runtime_inspector_recorder *rec;
    const runtime_inspector_sample *sample;
    if (rt == NULL || out == NULL || rt->inspector_recorder == NULL) return false;
    rec = rt->inspector_recorder;
    if (ordinal >= rec->count) return false;
    sample = inspector_sample_at_const(rec, (uint32_t)ordinal);
    *out = sample->meta;
    out->picture_available = runtime_frame_ring_has_picture_id(
        (runtime_frame_ring *)&rt->frame_ring, sample->meta.picture_id) ? 1u : 0u;
    return true;
}

bool runtime_inspector_sample_meta_by_id(
    const runtime *rt, uint64_t sample_id, runtime_inspector_sample_meta *out,
    uint64_t *ordinal)
{
    uint32_t i;
    const struct runtime_inspector_recorder *rec;
    if (rt == NULL || sample_id == 0u || rt->inspector_recorder == NULL) return false;
    rec = rt->inspector_recorder;
    for (i = 0u; i < rec->count; i++) {
        const runtime_inspector_sample *sample = inspector_sample_at_const(rec, i);
        if (sample->meta.sample_id == sample_id) {
            if (out != NULL) *out = sample->meta;
            if (ordinal != NULL) *ordinal = i;
            return true;
        }
    }
    return false;
}

uint64_t runtime_inspector_checkpoints_dropped(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL ?
        rt->inspector_recorder->dropped : 0u;
}

uint64_t runtime_inspector_media_truncations(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL ?
        rt->inspector_recorder->truncations : 0u;
}

bool runtime_inspector_recorder_is_recording(const runtime *rt)
{
    return rt != NULL && rt->inspector_recorder != NULL &&
        rt->inspector_recorder->recording;
}

void runtime_inspector_fill_window_extras(
    const runtime *rt, runtime_inspector_window *out)
{
    if (rt == NULL || out == NULL || rt->inspector_recorder == NULL) return;
    out->checkpoint_count = rt->inspector_recorder->count;
    out->checkpoints_dropped = rt->inspector_recorder->dropped;
    out->media_truncations = rt->inspector_recorder->truncations;
    out->start_kind = rt->inspector_recorder->start_kind;
    out->start_arg1 = rt->inspector_recorder->start_arg1;
    out->epoch = rt->inspector_recorder->timeline_generation;
}

void runtime_inspector_apply_logged_inputs(
    runtime *rt, apple2_t *dst, uint64_t from_inclusive, uint64_t to_inclusive)
{
    struct runtime_inspector_recorder *rec;
    uint32_t tail;
    if (rt == NULL || dst == NULL || rt->inspector_recorder == NULL ||
        to_inclusive < from_inclusive) return;
    rec = rt->inspector_recorder;
    tail = (rec->input_head + rec->input_cap - rec->input_count) % rec->input_cap;
    while (rec->replay_i < rec->input_count) {
        const runtime_inspector_input *ev =
            &rec->inputs[(tail + rec->replay_i) % rec->input_cap];
        if (ev->boundary_cycle < from_inclusive) { rec->replay_i++; continue; }
        if (ev->boundary_cycle > to_inclusive) break;
        inspector_apply_event(dst, ev);
        rec->replay_i++;
    }
}

bool runtime_inspector_load_nearest_checkpoint(runtime *rt, uint64_t cycle)
{
    struct runtime_inspector_recorder *rec;
    const runtime_inspector_sample *sample;
    uint64_t watermark;
    uint8_t execution_mode;
    if (rt == NULL || !rt->machine_ready || rt->inspector_recorder == NULL) return false;
    rec = rt->inspector_recorder;
    sample = inspector_nearest_sample(rec, cycle);
    if (sample != NULL) {
        watermark = sample->meta.snapshot_replay_watermark;
        execution_mode = sample->meta.execution_mode;
        if (!inspector_replay_from_anchor(
                rt, &rt->machine, sample->meta.snapshot_cycle, watermark,
                0u, NULL, NULL, rec->work_framebuffer) ||
            !apple2_snapshot_load(&rt->machine, sample->blob, sample->size)) {
            return false;
        }
        memcpy(
            rt->machine.video.fb, rec->work_framebuffer,
            inspector_framebuffer_bytes);
    } else if (rec->anchor.valid && rec->anchor.snapshot_cycle <= cycle) {
        watermark = rec->anchor.snapshot_replay_watermark;
        execution_mode = rec->anchor.execution_mode;
        if (!apple2_snapshot_load(
                &rt->machine, rec->anchor.blob, rec->anchor.size)) {
            return false;
        }
        memcpy(
            rt->machine.video.fb, rec->anchor.resume_framebuffer,
            inspector_framebuffer_bytes);
    } else {
        return false;
    }
    rt->machine.video.paint_enabled =
        execution_mode == RUNTIME_INSPECTOR_EXECUTION_FINITE;
    apple2_set_replay_sealed(&rt->machine, true);
    apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    apple2_set_memory_access_callback(&rt->machine, NULL, NULL);
    inspector_replay_cursor_reset(rec, watermark);
    return true;
}

bool runtime_inspector_reconstruct_sample_picture(
    runtime *rt,
    uint64_t sample_id,
    uint32_t *out_pixels,
    size_t out_pixel_count)
{
    struct runtime_inspector_recorder *rec;
    runtime_inspector_sample *target;
    uint64_t ordinal;
    bool captured = false;

    if (rt == NULL || out_pixels == NULL ||
        out_pixel_count < (size_t)RUNTIME_FRAME_RING_PIXELS ||
        rt->inspector_recorder == NULL ||
        !runtime_inspector_sample_meta_by_id(rt, sample_id, NULL, &ordinal)) {
        return false;
    }
    rec = rt->inspector_recorder;
    target = inspector_sample_at(rec, (uint32_t)ordinal);
    if (!inspector_replay_from_anchor(
            rt, &rt->machine, target->meta.snapshot_cycle,
            target->meta.snapshot_replay_watermark,
            target->meta.frame_cycle,
            target->meta.kind == RUNTIME_INSPECTOR_SAMPLE_FINITE_FRAME ?
                out_pixels : NULL,
            &captured, rec->work_framebuffer)) {
        return false;
    }
    if (!apple2_snapshot_load(&rt->machine, target->blob, target->size)) {
        return false;
    }
    memcpy(
        rt->machine.video.fb, rec->work_framebuffer,
        inspector_framebuffer_bytes);
    rt->machine.video.paint_enabled =
        target->meta.execution_mode == RUNTIME_INSPECTOR_EXECUTION_FINITE;
    if (target->meta.kind == RUNTIME_INSPECTOR_SAMPLE_MAX_FRAME) {
        captured = apple2_video_paint_full_frame_to(
            &rt->machine, out_pixels, out_pixel_count);
    }
    apple2_set_replay_sealed(&rt->machine, true);
    apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    apple2_set_memory_access_callback(&rt->machine, NULL, NULL);
    inspector_replay_cursor_reset(rec, target->meta.snapshot_replay_watermark);
    return captured;
}

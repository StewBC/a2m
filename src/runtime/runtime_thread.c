#include "runtime_internal.h"

#include "apple2.h"
#include "apple2_file.h"
#include "apple2_snapshot.h"
#include "apple_type_script.h"
#include "audio_buffer.h"
#include "mboard.h"
#include "message_queue.h"
#include "runtime_breakpoint_ini.h"
#include "runtime_assembler.h"
#include "runtime_history_wire.h"
#include "runtime_inspector.h"
#include "softswitch.h"
#include "video.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void runtime_publish_argb_pixels(
    runtime *rt,
    const uint32_t *pixels,
    runtime_frame_publish_kind kind,
    uint64_t inspector_picture_id);
static void runtime_publish_canonical_frame(
    runtime *rt,
    runtime_frame_publish_kind kind,
    uint64_t inspector_picture_id);
static void runtime_publish_presented_frame(runtime *rt);
static void runtime_set_active_turbo(runtime *rt, uint32_t milli_mhz);

/* One TYPE wait unit ≈ 10 ms at ~1 MHz (product pacing, not cycle-perfect). */
enum { RUNTIME_TYPE_WAIT_CYCLES_PER_UNIT = 10000u };

/* History record kinds must match machine observer kinds. */
typedef char runtime_history_observer_kind_values_must_match[
    (int)APPLE2_CPU_OBSERVER_INSTRUCTION ==
            (int)RUNTIME_HISTORY_RECORD_INSTRUCTION &&
    (int)APPLE2_CPU_OBSERVER_IRQ == (int)RUNTIME_HISTORY_RECORD_IRQ &&
    (int)APPLE2_CPU_OBSERVER_NMI == (int)RUNTIME_HISTORY_RECORD_NMI ? 1 : -1];

/* Bus access kind numbers must match between cpu65 and history wire. */
typedef char runtime_history_bus_kind_values_must_match[
    (int)CPU65_BUS_ACCESS_DATA_READ == (int)C6510_BUS_ACCESS_DATA_READ &&
    (int)CPU65_BUS_ACCESS_OPCODE_FETCH == (int)C6510_BUS_ACCESS_OPCODE_FETCH &&
    (int)CPU65_BUS_ACCESS_VECTOR_READ == (int)C6510_BUS_ACCESS_VECTOR_READ
        ? 1
        : -1];

static void runtime_publish_machine(runtime *rt);
static void runtime_publish_cpu(runtime *rt, uint64_t request_token);
static void runtime_publish_simple(runtime *rt, runtime_event_type type);
static void runtime_publish_event(runtime *rt, const runtime_event *event);
static void runtime_type_script_tick(runtime *rt, uint32_t cycles_elapsed);
static void runtime_history_sync_observer(runtime *rt);
static void runtime_reset_pacer(runtime *rt);
static void runtime_inspector_publish_head(runtime *rt);

static void runtime_history_observer_begin(
    void *user,
    const apple2_cpu_observer_begin *observed)
{
    runtime *rt = (runtime *)user;
    runtime_history_begin begin;

    if (rt == NULL || observed == NULL || rt->history == NULL) {
        return;
    }
    begin.kind = (runtime_history_record_kind)observed->kind;
    begin.machine_cycle = observed->machine_cycle;
    begin.pc = observed->pc;
    begin.a = observed->a;
    begin.x = observed->x;
    begin.y = observed->y;
    begin.sp = observed->sp;
    begin.p = observed->p;
    (void)runtime_history_begin_record(rt->history, &begin);
}

static void runtime_history_observer_access(
    void *user,
    uint64_t machine_cycle,
    uint16_t address,
    uint8_t value,
    cpu65_bus_access_kind kind)
{
    runtime *rt = (runtime *)user;
    if (rt == NULL || rt->history == NULL) {
        return;
    }
    (void)runtime_history_append_observed_access(
        rt->history,
        (c6510_bus_access_kind)kind,
        address,
        value,
        machine_cycle);
}

static void runtime_history_observer_complete(void *user)
{
    runtime *rt = (runtime *)user;
    if (rt == NULL) {
        return;
    }
    if (rt->history != NULL) {
        (void)runtime_history_complete_record(rt->history);
    }
    /* TRON: log completed instruction using pre-complete CPU state after step. */
    if (rt->trace_enabled && rt->trace_file != NULL) {
        fprintf(
            rt->trace_file,
            "PC=%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X CYC=%llu\n",
            rt->machine.cpu.cpu.opcode_pc,
            rt->machine.cpu.cpu.A,
            rt->machine.cpu.cpu.X,
            rt->machine.cpu.cpu.Y,
            (unsigned)(rt->machine.cpu.cpu.sp & 0xffu),
            rt->machine.cpu.cpu.flags,
            (unsigned long long)apple2_cycles(&rt->machine));
    }
}

static const apple2_cpu_observer runtime_history_observer = {
    .begin = runtime_history_observer_begin,
    .access = runtime_history_observer_access,
    .complete = runtime_history_observer_complete,
};

static void runtime_history_sync_observer(runtime *rt)
{
    runtime_history_status status;

    if (rt == NULL) {
        return;
    }
    if (rt->history == NULL) {
        apple2_set_cpu_observer(&rt->machine, NULL, NULL);
        return;
    }
    runtime_history_get_status(rt->history, &status);
    if (status.available && status.recording) {
        apple2_set_cpu_observer(&rt->machine, &runtime_history_observer, rt);
    } else {
        apple2_set_cpu_observer(&rt->machine, NULL, NULL);
    }
}

static void runtime_history_prepare_discontinuity(runtime *rt)
{
    if (rt == NULL || rt->history == NULL) {
        return;
    }
    (void)runtime_history_seal_partial(rt->history);
}

static void runtime_publish_history_status(runtime *rt, uint64_t request_token)
{
    runtime_event event;

    if (rt == NULL) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_HISTORY_STATUS_RESPONSE;
    event.request_token = request_token;
    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &event.data.history_status);
    } else {
        event.data.history_status.available = false;
        event.data.history_status.recording = false;
        event.data.history_status.unavailable_reason =
            RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG;
    }
    runtime_publish_event(rt, &event);
}

static void runtime_history_invalidate_cursor(runtime *rt)
{
    size_t i;

    if (rt == NULL) {
        return;
    }
    rt->history_mutation_generation++;
    if (rt->history_mutation_generation == 0u) {
        rt->history_mutation_generation = 1u;
    }
    for (i = 0u; i < RUNTIME_SESSION_CAPACITY; ++i) {
        if (rt->sessions[i].active && rt->sessions[i].history_cursor.active) {
            rt->sessions[i].history_cursor.active = 0u;
            rt->sessions[i].history_cursor.stale = 1u;
        }
    }
}

static runtime_state_changed_reason runtime_state_changed_reason_for_command(
    runtime_command_type type)
{
    switch (type) {
    case RUNTIME_COMMAND_STEP_CYCLE:
    case RUNTIME_COMMAND_STEP_INSTRUCTION:
    case RUNTIME_COMMAND_STEP_OVER:
    case RUNTIME_COMMAND_STEP_OUT:
        return RUNTIME_STATE_CHANGED_STEP;
    case RUNTIME_COMMAND_RUN:
    case RUNTIME_COMMAND_RUN_CYCLES:
    case RUNTIME_COMMAND_RUN_INSTRUCTIONS:
    case RUNTIME_COMMAND_RUN_TO_CURSOR:
        return RUNTIME_STATE_CHANGED_RUN;
    case RUNTIME_COMMAND_PAUSE:
        return RUNTIME_STATE_CHANGED_PAUSE;
    case RUNTIME_COMMAND_WRITE_MEMORY_BYTE:
    case RUNTIME_COMMAND_WRITE_MEMORY:
    case RUNTIME_COMMAND_SET_CPU_REGISTER:
        return RUNTIME_STATE_CHANGED_POKE;
    case RUNTIME_COMMAND_RESET:
        return RUNTIME_STATE_CHANGED_RESET;
    case RUNTIME_COMMAND_LOAD_STATE:
    case RUNTIME_COMMAND_LOAD_BIN:
        return RUNTIME_STATE_CHANGED_LOAD_STATE;
    case RUNTIME_COMMAND_HISTORY_CLEAR:
    case RUNTIME_COMMAND_HISTORY_RECORD:
        return RUNTIME_STATE_CHANGED_HISTORY_CLEAR;
    case RUNTIME_COMMAND_MEDIA_INSERT:
    case RUNTIME_COMMAND_MEDIA_EJECT:
    case RUNTIME_COMMAND_MEDIA_SWAP:
    case RUNTIME_COMMAND_BOOT_SLOT:
        return RUNTIME_STATE_CHANGED_MEDIA;
    default:
        return RUNTIME_STATE_CHANGED_OTHER;
    }
}

static void runtime_publish_state_changed(
    runtime *rt,
    runtime_state_changed_reason reason,
    uint32_t source_session_id)
{
    runtime_event event;
    runtime_history_status history_status;

    if (rt == NULL) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_STATE_CHANGED;
    event.request_token = 0u;
    event.data.state_changed.reason = reason;
    event.data.state_changed.source_session_id = source_session_id;
    if (rt->machine_ready) {
        event.data.state_changed.cycles = apple2_cycles(&rt->machine);
    }
    event.data.state_changed.frame = rt->frame_slot.frame_number;
    if (rt->history != NULL) {
        runtime_history_get_status(rt->history, &history_status);
        event.data.state_changed.history_epoch = history_status.epoch;
    }
    runtime_publish_event(rt, &event);
}

static uint64_t runtime_history_allocate_cursor_id(runtime *rt)
{
    uint64_t id = ++rt->next_history_cursor_id;
    if (id == 0u) {
        id = ++rt->next_history_cursor_id;
    }
    return id;
}

static runtime_session *runtime_session_lookup(runtime *rt, uint32_t session_id)
{
    size_t i;

    if (rt == NULL || session_id == 0u) {
        return NULL;
    }
    for (i = 0u; i < RUNTIME_SESSION_CAPACITY; ++i) {
        if (rt->sessions[i].active && rt->sessions[i].id == session_id) {
            return &rt->sessions[i];
        }
    }
    return NULL;
}

/* session_id 0 → default session. */
static runtime_session *runtime_session_resolve(runtime *rt, uint32_t session_id)
{
    if (rt == NULL) {
        return NULL;
    }
    if (session_id == 0u) {
        session_id = rt->default_session_id;
    }
    return runtime_session_lookup(rt, session_id);
}

static uint32_t runtime_session_allocate_id(runtime *rt)
{
    uint32_t id = ++rt->next_session_id;
    if (id == 0u) {
        id = ++rt->next_session_id;
    }
    return id;
}

static runtime_session *runtime_session_allocate(
    runtime *rt,
    runtime_session_kind kind,
    uint64_t endpoint_epoch)
{
    size_t i;

    if (rt == NULL ||
        (kind != RUNTIME_SESSION_KIND_UI &&
         kind != RUNTIME_SESSION_KIND_CONTROL)) {
        return NULL;
    }
    for (i = 0u; i < RUNTIME_SESSION_CAPACITY; ++i) {
        if (!rt->sessions[i].active) {
            memset(&rt->sessions[i], 0, sizeof(rt->sessions[i]));
            rt->sessions[i].id = runtime_session_allocate_id(rt);
            rt->sessions[i].kind = kind;
            rt->sessions[i].active = 1u;
            rt->sessions[i].endpoint_epoch = endpoint_epoch;
            return &rt->sessions[i];
        }
    }
    return NULL;
}

static bool runtime_session_release(runtime *rt, uint32_t session_id)
{
    runtime_session *session;

    if (rt == NULL || session_id == 0u) {
        return false;
    }
    /* Never release the default compat session. */
    if (session_id == rt->default_session_id) {
        session = runtime_session_lookup(rt, session_id);
        if (session != NULL) {
            memset(&session->history_cursor, 0, sizeof(session->history_cursor));
        }
        return true;
    }
    session = runtime_session_lookup(rt, session_id);
    if (session == NULL) {
        return false;
    }
    memset(session, 0, sizeof(*session));
    return true;
}

static void runtime_publish_session_response(
    runtime *rt,
    uint64_t request_token,
    runtime_session_status status,
    uint32_t session_id,
    runtime_session_kind kind)
{
    runtime_event event;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_SESSION_RESPONSE;
    event.request_token = request_token;
    event.data.session.status = status;
    event.data.session.session_id = session_id;
    event.data.session.kind = (uint8_t)kind;
    runtime_publish_event(rt, &event);
}

static bool runtime_history_payload_active(runtime *rt)
{
    runtime_rpc_payload_pool *pool;
    size_t i;
    bool active = false;

    if (rt == NULL) {
        return false;
    }
    pool = &rt->rpc_payload_pool;
    if (pool->mutex == NULL) {
        return false;
    }
    mutex_lock(pool->mutex);
    for (i = 0u; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; ++i) {
        if (pool->slots[i].in_use &&
            pool->slots[i].kind == RUNTIME_RPC_PAYLOAD_HISTORY) {
            active = true;
            break;
        }
    }
    mutex_unlock(pool->mutex);
    return active;
}

static void runtime_publish_history_rpc_status(
    runtime *rt,
    uint64_t request_token,
    runtime_history_rpc_status status)
{
    runtime_event event;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_HISTORY_RESULT_RESPONSE;
    event.request_token = request_token;
    event.data.history_rpc.status = status;
    runtime_publish_event(rt, &event);
}

static bool runtime_publish_history_payload(
    runtime *rt,
    uint64_t request_token,
    uint8_t *bytes,
    uint32_t byte_length,
    const runtime_history_rpc_meta *meta)
{
    runtime_rpc_payload_pool *pool = &rt->rpc_payload_pool;
    runtime_event event;
    size_t slot_index = RUNTIME_RPC_PAYLOAD_POOL_CAPACITY;
    size_t i;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_HISTORY_RESULT_RESPONSE;
    event.request_token = request_token;

    if (request_token == 0u || bytes == NULL || byte_length == 0u ||
        meta == NULL || pool->mutex == NULL) {
        free(bytes);
        runtime_publish_history_rpc_status(
            rt, request_token, RUNTIME_HISTORY_RPC_ERROR);
        return false;
    }
    mutex_lock(pool->mutex);
    for (i = 0u; i < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; ++i) {
        if (!pool->slots[i].in_use) {
            slot_index = i;
            break;
        }
    }
    if (slot_index == RUNTIME_RPC_PAYLOAD_POOL_CAPACITY) {
        mutex_unlock(pool->mutex);
        free(bytes);
        runtime_publish_history_rpc_status(
            rt, request_token, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        return false;
    }
    pool->slots[slot_index].request_token = request_token;
    pool->slots[slot_index].length = byte_length;
    pool->slots[slot_index].kind = RUNTIME_RPC_PAYLOAD_HISTORY;
    pool->slots[slot_index].meta.history = *meta;
    pool->slots[slot_index].bytes = bytes;
    pool->slots[slot_index].in_use = 1u;
    mutex_unlock(pool->mutex);

    event.data.history_rpc = *meta;
    if (rt->event_queue == NULL ||
        !message_queue_push(rt->event_queue, &event)) {
        mutex_lock(pool->mutex);
        if (pool->slots[slot_index].in_use &&
            pool->slots[slot_index].request_token == request_token &&
            pool->slots[slot_index].kind == RUNTIME_RPC_PAYLOAD_HISTORY) {
            free(pool->slots[slot_index].bytes);
            memset(&pool->slots[slot_index], 0, sizeof(pool->slots[slot_index]));
        }
        mutex_unlock(pool->mutex);
        runtime_publish_history_rpc_status(
            rt, request_token, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        return false;
    }
    return true;
}

static runtime_history_rpc_status runtime_history_map_query_result(
    runtime_history_query_result result)
{
    switch (result) {
    case RUNTIME_HISTORY_QUERY_OK:
        return RUNTIME_HISTORY_RPC_OK;
    case RUNTIME_HISTORY_QUERY_UNAVAILABLE:
        return RUNTIME_HISTORY_RPC_UNAVAILABLE;
    case RUNTIME_HISTORY_QUERY_EPOCH_MISMATCH:
        return RUNTIME_HISTORY_RPC_EPOCH_MISMATCH;
    case RUNTIME_HISTORY_QUERY_RECORD_NOT_RETAINED:
        return RUNTIME_HISTORY_RPC_RECORD_NOT_RETAINED;
    case RUNTIME_HISTORY_QUERY_INVALID:
        return RUNTIME_HISTORY_RPC_BAD_ARGS;
    case RUNTIME_HISTORY_QUERY_FAILED:
    default:
        return RUNTIME_HISTORY_RPC_ERROR;
    }
}

static void runtime_history_page_bounds(
    const runtime_history_record *records,
    size_t count,
    uint64_t *out_oldest,
    uint64_t *out_newest)
{
    size_t i;
    uint64_t oldest = 0u;
    uint64_t newest = 0u;

    for (i = 0u; i < count; ++i) {
        if (oldest == 0u || records[i].id < oldest) {
            oldest = records[i].id;
        }
        if (records[i].id > newest) {
            newest = records[i].id;
        }
    }
    *out_oldest = oldest;
    *out_newest = newest;
}

static void runtime_history_find_command(
    runtime *rt,
    const runtime_command *command)
{
    const runtime_history_query *query = &command->data.history_find.query;
    runtime_session *session;
    runtime_history_cursor *cursor;
    runtime_history_record *records;
    runtime_history_page page;
    runtime_history_query_result query_result;
    runtime_history_wire_result wire_result;
    runtime_history_rpc_meta meta;
    runtime_history_status status;
    uint8_t *bytes = NULL;
    uint32_t byte_length = 0u;
    size_t encoded_count = 0u;
    bool clipped = false;
    uint64_t from_id;

    session = runtime_session_resolve(rt, command->data.history_find.session_id);
    if (session == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_BAD_ARGS);
        return;
    }
    cursor = &session->history_cursor;

    if (rt->history == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_UNAVAILABLE);
        return;
    }
    runtime_history_get_status(rt->history, &status);
    if (!status.available) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_UNAVAILABLE);
        return;
    }
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_MACHINE_RUNNING);
        return;
    }
    if (runtime_history_payload_active(rt)) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        return;
    }
    memset(cursor, 0, sizeof(*cursor));
    records = (runtime_history_record *)calloc(
        command->data.history_find.limit, sizeof(*records));
    if (records == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    switch ((runtime_history_from_kind)command->data.history_find.from_kind) {
    case RUNTIME_HISTORY_FROM_OLDEST:
        from_id = status.oldest_id;
        break;
    case RUNTIME_HISTORY_FROM_NEWEST:
        from_id = status.newest_id;
        break;
    case RUNTIME_HISTORY_FROM_ID:
        from_id = command->data.history_find.from_id;
        if (from_id == 0u) {
            free(records);
            runtime_publish_history_rpc_status(
                rt, command->request_token, RUNTIME_HISTORY_RPC_BAD_ARGS);
            return;
        }
        break;
    case RUNTIME_HISTORY_FROM_DEFAULT:
        from_id = 0u;
        break;
    default:
        free(records);
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_BAD_ARGS);
        return;
    }
    query_result = runtime_history_find(
        rt->history,
        query,
        from_id,
        command->data.history_find.limit,
        records,
        &page,
        NULL);
    if (query_result != RUNTIME_HISTORY_QUERY_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt,
            command->request_token,
            runtime_history_map_query_result(query_result));
        return;
    }
    wire_result = runtime_history_wire_encode(
        query->has_epoch ? query->epoch : status.epoch,
        records,
        page.count,
        true,
        0u,
        &bytes,
        &byte_length,
        &encoded_count,
        &clipped);
    if (wire_result != RUNTIME_HISTORY_WIRE_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    memset(&meta, 0, sizeof(meta));
    meta.status = RUNTIME_HISTORY_RPC_OK;
    meta.byte_length = byte_length;
    meta.epoch = query->has_epoch ? query->epoch : status.epoch;
    meta.count = (uint32_t)encoded_count;
    runtime_history_page_bounds(
        records, encoded_count, &meta.oldest, &meta.newest);
    meta.more = (page.more || clipped) ? 1u : 0u;
    if (meta.more) {
        cursor->query = *query;
        cursor->query.has_epoch = true;
        cursor->query.epoch = meta.epoch;
        cursor->id = runtime_history_allocate_cursor_id(rt);
        cursor->epoch = meta.epoch;
        cursor->mutation_generation = rt->history_mutation_generation;
        cursor->next_id = page.next_id;
        cursor->active = 1u;
        meta.cursor = cursor->id;
    }
    free(records);
    (void)runtime_publish_history_payload(
        rt, command->request_token, bytes, byte_length, &meta);
}

static void runtime_history_next_command(
    runtime *rt,
    const runtime_command *command)
{
    runtime_session *session;
    runtime_history_cursor *cursor;
    runtime_history_record *records;
    runtime_history_page page;
    runtime_history_query_result query_result;
    runtime_history_rpc_meta meta;
    uint8_t *bytes = NULL;
    uint32_t byte_length = 0u;
    size_t encoded_count = 0u;
    bool clipped = false;

    session = runtime_session_resolve(rt, command->data.history_next.session_id);
    if (session == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_BAD_ARGS);
        return;
    }
    cursor = &session->history_cursor;

    if (rt->history == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_UNAVAILABLE);
        return;
    }
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_MACHINE_RUNNING);
        return;
    }
    if (runtime_history_payload_active(rt)) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        return;
    }
    if (cursor->id != command->data.history_next.cursor ||
        !cursor->active || cursor->stale ||
        cursor->mutation_generation != rt->history_mutation_generation) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_CURSOR_STALE);
        return;
    }
    records = (runtime_history_record *)calloc(
        command->data.history_next.limit, sizeof(*records));
    if (records == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    query_result = runtime_history_find(
        rt->history,
        &cursor->query,
        cursor->next_id,
        command->data.history_next.limit,
        records,
        &page,
        NULL);
    if (query_result != RUNTIME_HISTORY_QUERY_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt,
            command->request_token,
            runtime_history_map_query_result(query_result));
        return;
    }
    if (runtime_history_wire_encode(
            cursor->epoch,
            records,
            page.count,
            true,
            0u,
            &bytes,
            &byte_length,
            &encoded_count,
            &clipped) != RUNTIME_HISTORY_WIRE_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    memset(&meta, 0, sizeof(meta));
    meta.status = RUNTIME_HISTORY_RPC_OK;
    meta.byte_length = byte_length;
    meta.epoch = cursor->epoch;
    meta.count = (uint32_t)encoded_count;
    runtime_history_page_bounds(
        records, encoded_count, &meta.oldest, &meta.newest);
    meta.more = (page.more || clipped) ? 1u : 0u;
    if (meta.more) {
        cursor->next_id = page.next_id;
        meta.cursor = cursor->id;
    } else {
        cursor->active = 0u;
        cursor->stale = 0u;
    }
    free(records);
    (void)runtime_publish_history_payload(
        rt, command->request_token, bytes, byte_length, &meta);
}

static void runtime_history_read_command(
    runtime *rt,
    const runtime_command *command)
{
    runtime_history_record *records;
    runtime_history_page page;
    runtime_history_query_result query_result;
    runtime_history_rpc_meta meta;
    runtime_history_status status;
    uint64_t epoch;
    uint8_t *bytes = NULL;
    uint32_t byte_length = 0u;
    size_t encoded_count = 0u;
    bool clipped = false;

    if (rt->history == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_UNAVAILABLE);
        return;
    }
    runtime_history_get_status(rt->history, &status);
    if (!status.available) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_UNAVAILABLE);
        return;
    }
    if (rt->exec_state != RUNTIME_EXEC_PAUSED) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_MACHINE_RUNNING);
        return;
    }
    if (runtime_history_payload_active(rt)) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_REQUEST_ACTIVE);
        return;
    }
    records = (runtime_history_record *)calloc(
        RUNTIME_HISTORY_MAX_CONTEXT_RECORDS, sizeof(*records));
    if (records == NULL) {
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    epoch = command->data.history_read.epoch != 0u ?
        command->data.history_read.epoch :
        status.epoch;
    query_result = runtime_history_read(
        rt->history,
        epoch,
        command->data.history_read.id,
        command->data.history_read.before,
        command->data.history_read.after,
        records,
        RUNTIME_HISTORY_MAX_CONTEXT_RECORDS,
        &page);
    if (query_result != RUNTIME_HISTORY_QUERY_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt,
            command->request_token,
            runtime_history_map_query_result(query_result));
        return;
    }
    if (runtime_history_wire_encode(
            epoch,
            records,
            page.count,
            false,
            command->data.history_read.id,
            &bytes,
            &byte_length,
            &encoded_count,
            &clipped) != RUNTIME_HISTORY_WIRE_OK) {
        free(records);
        runtime_publish_history_rpc_status(
            rt, command->request_token, RUNTIME_HISTORY_RPC_ERROR);
        return;
    }
    memset(&meta, 0, sizeof(meta));
    meta.status = RUNTIME_HISTORY_RPC_OK;
    meta.byte_length = byte_length;
    meta.epoch = epoch;
    meta.count = (uint32_t)encoded_count;
    runtime_history_page_bounds(
        records, encoded_count, &meta.oldest, &meta.newest);
    meta.more = (page.more || clipped) ? 1u : 0u;
    free(records);
    (void)runtime_publish_history_payload(
        rt, command->request_token, bytes, byte_length, &meta);
}

static bool runtime_history_command_invalidates_cursor(runtime_command_type type)
{
    switch (type) {
    case RUNTIME_COMMAND_RESET:
    case RUNTIME_COMMAND_RUN:
    case RUNTIME_COMMAND_STEP_CYCLE:
    case RUNTIME_COMMAND_STEP_INSTRUCTION:
    case RUNTIME_COMMAND_STEP_OVER:
    case RUNTIME_COMMAND_STEP_OUT:
    case RUNTIME_COMMAND_RUN_CYCLES:
    case RUNTIME_COMMAND_RUN_INSTRUCTIONS:
    case RUNTIME_COMMAND_RUN_TO_CURSOR:
    case RUNTIME_COMMAND_WRITE_MEMORY_BYTE:
    case RUNTIME_COMMAND_WRITE_MEMORY:
    case RUNTIME_COMMAND_SET_CPU_REGISTER:
    case RUNTIME_COMMAND_HISTORY_RECORD:
    case RUNTIME_COMMAND_INSPECTOR_SET_ENABLED:
    case RUNTIME_COMMAND_HISTORY_CLEAR:
    case RUNTIME_COMMAND_SAVE_STATE:
    case RUNTIME_COMMAND_LOAD_STATE:
    case RUNTIME_COMMAND_LOAD_BIN:
    case RUNTIME_COMMAND_ASSEMBLE_FILE:
    case RUNTIME_COMMAND_MEDIA_INSERT:
    case RUNTIME_COMMAND_MEDIA_EJECT:
    case RUNTIME_COMMAND_MEDIA_SWAP:
    case RUNTIME_COMMAND_BOOT_SLOT:
    case RUNTIME_COMMAND_APPLY_MACHINE_CONFIG:
        return true;
    default:
        return false;
    }
}

static void runtime_type_script_stop(runtime *rt)
{
    if (rt == NULL) {
        return;
    }
    rt->type_script_active = false;
    rt->type_script_await_paste = false;
    rt->type_script_index = 0;
    rt->type_script_count = 0;
    rt->type_script_wait_left = 0;
    rt->type_script_wait_cycle_accum = 0;
}

static void runtime_type_script_start(runtime *rt, const char *text)
{
    apple_type_parse_error err;
    size_t count = 0;

    if (rt == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    memset(&err, 0, sizeof(err));
    err.offset = -1;
    if (!apple_type_script_parse(
            text,
            rt->type_script_events,
            APPLE_TYPE_EVENTS_MAX,
            &count,
            &err) ||
        count == 0u) {
        return;
    }

    apple2_paste_cancel(&rt->machine);
    rt->type_script_active = true;
    rt->type_script_await_paste = false;
    rt->type_script_index = 0;
    rt->type_script_count = count;
    rt->type_script_wait_left = 0;
    rt->type_script_wait_cycle_accum = 0;
    /* TYPE does not change turbo (zip policy). */
    /* Apply immediate events (axes/buttons/OA) even if BREAK pauses next. */
    runtime_type_script_tick(rt, 0u);
}

static void runtime_type_script_apply_axis(runtime *rt, uint8_t stick, uint8_t axis, uint8_t value)
{
    int idx;
    if (stick > 1u || axis > 1u) {
        return;
    }
    idx = (int)stick * 2 + (int)axis;
    apple2_gameport_set_axis(&rt->machine, idx, value);
}

static void runtime_type_script_apply_button(runtime *rt, uint8_t btn, uint8_t pressed)
{
    uint8_t mask = rt->machine.gameport_buttons;
    uint8_t bit = (btn == 0u) ? APPLE2_GAMEPORT_BUTTON0 : APPLE2_GAMEPORT_BUTTON1;
    if (pressed) {
        mask = (uint8_t)(mask | bit);
    } else {
        mask = (uint8_t)(mask & (uint8_t)~bit);
    }
    apple2_gameport_set_buttons(&rt->machine, mask);
}

/* Advance TYPE script: call from free-run and after steps. */
static void runtime_type_script_tick(runtime *rt, uint32_t cycles_elapsed)
{
    if (rt == NULL || !rt->type_script_active) {
        return;
    }

    if (rt->type_script_await_paste) {
        if (apple2_paste_active(&rt->machine)) {
            return;
        }
        rt->type_script_await_paste = false;
    }

    if (rt->type_script_wait_left > 0u) {
        rt->type_script_wait_cycle_accum += cycles_elapsed;
        while (rt->type_script_wait_left > 0u &&
               rt->type_script_wait_cycle_accum >= RUNTIME_TYPE_WAIT_CYCLES_PER_UNIT) {
            rt->type_script_wait_cycle_accum -= RUNTIME_TYPE_WAIT_CYCLES_PER_UNIT;
            rt->type_script_wait_left--;
        }
        if (rt->type_script_wait_left > 0u) {
            return;
        }
    }

    while (rt->type_script_index < rt->type_script_count) {
        const apple_type_event *ev = &rt->type_script_events[rt->type_script_index];

        switch ((apple_type_event_kind)ev->kind) {
        case APPLE_TYPE_EV_CHAR: {
            /* Batch consecutive chars into one paste feed. */
            char batch[APPLE_TYPE_EVENTS_MAX + 1u];
            size_t blen = 0;
            size_t j = rt->type_script_index;
            while (j < rt->type_script_count &&
                   rt->type_script_events[j].kind == (uint8_t)APPLE_TYPE_EV_CHAR &&
                   blen + 1u < sizeof(batch)) {
                batch[blen++] = (char)rt->type_script_events[j].value;
                j++;
            }
            batch[blen] = '\0';
            rt->type_script_index = j;
            if (blen > 0u && apple2_paste_begin(&rt->machine, batch, blen)) {
                rt->type_script_await_paste = true;
                return;
            }
            break;
        }
        case APPLE_TYPE_EV_OA_SET:
            if (ev->value) {
                rt->machine.state_flags |= A2S_OPEN_APPLE;
            } else {
                rt->machine.state_flags &= ~A2S_OPEN_APPLE;
            }
            rt->type_script_index++;
            break;
        case APPLE_TYPE_EV_CA_SET:
            if (ev->value) {
                rt->machine.state_flags |= A2S_CLOSED_APPLE;
            } else {
                rt->machine.state_flags &= ~A2S_CLOSED_APPLE;
            }
            rt->type_script_index++;
            break;
        case APPLE_TYPE_EV_BUTTON_SET:
            runtime_type_script_apply_button(rt, ev->axis_or_btn, ev->value);
            rt->type_script_index++;
            break;
        case APPLE_TYPE_EV_AXIS_SET:
            runtime_type_script_apply_axis(rt, ev->stick, ev->axis_or_btn, ev->value);
            rt->type_script_index++;
            break;
        case APPLE_TYPE_EV_WAIT:
            rt->type_script_wait_left = ev->wait_count;
            rt->type_script_wait_cycle_accum = 0;
            rt->type_script_index++;
            return;
        case APPLE_TYPE_EV_RESET_WARM:
            apple2_reset(&rt->machine);
            rt->suppress_execute_bp = false;
            rt->breakpoint_hit_pending = false;
            rt->type_script_index++;
            runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
            runtime_publish_cpu(rt, 0u);
            runtime_publish_machine(rt);
            break;
        case APPLE_TYPE_EV_RESET_COLD:
            apple2_cold_reset(&rt->machine);
            rt->suppress_execute_bp = false;
            rt->breakpoint_hit_pending = false;
            rt->type_script_index++;
            runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
            runtime_publish_cpu(rt, 0u);
            runtime_publish_machine(rt);
            break;
        default:
            rt->type_script_index++;
            break;
        }
    }

    if (rt->type_script_index >= rt->type_script_count &&
        !rt->type_script_await_paste &&
        rt->type_script_wait_left == 0u) {
        runtime_type_script_stop(rt);
    }
}

view_flags_t runtime_mode_to_view_flags(runtime_memory_mode mode)
{
    switch (mode) {
    case RUNTIME_MEMORY_MODE_MAIN:
        return view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);
    case RUNTIME_MEMORY_MODE_ROM:
        return view_flags_from_area(RUNTIME_VIEW_AREA_ROM);
    case RUNTIME_MEMORY_MODE_AUX:
        return view_flags_from_area(RUNTIME_VIEW_AREA_AUX);
    case RUNTIME_MEMORY_MODE_LC1:
        return view_flags_from_area(RUNTIME_VIEW_AREA_LC1);
    case RUNTIME_MEMORY_MODE_LC2:
        return view_flags_from_area(RUNTIME_VIEW_AREA_LC2);
    case RUNTIME_MEMORY_MODE_MAP:
    default:
        return view_flags_from_area(RUNTIME_VIEW_AREA_MAP);
    }
}

static void runtime_publish_event(runtime *rt, const runtime_event *event)
{
    if (rt == NULL || event == NULL || rt->event_queue == NULL) {
        return;
    }
    (void)message_queue_push(rt->event_queue, event);
}

static void runtime_publish_simple(runtime *rt, runtime_event_type type)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    runtime_publish_event(rt, &event);
}

static void runtime_publish_media_changed(
    runtime *rt,
    runtime_media_change_type change_type,
    uint8_t slot,
    uint8_t device,
    runtime_slot_card_type card_type,
    bool success,
    const char *path)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_MEDIA_CHANGED;
    event.data.media_changed.change_type = (uint8_t)change_type;
    event.data.media_changed.slot = slot;
    event.data.media_changed.device = device;
    event.data.media_changed.card_type = (uint8_t)card_type;
    event.data.media_changed.success = success ? 1u : 0u;
    if (path != NULL) {
        snprintf(event.data.media_changed.path,
            sizeof(event.data.media_changed.path), "%s", path);
    }
    runtime_publish_event(rt, &event);
}

static void runtime_publish_error(runtime *rt, const char *msg)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_ERROR;
    if (msg != NULL) {
        strncpy(event.data.error.message, msg, sizeof(event.data.error.message) - 1);
    }
    runtime_publish_event(rt, &event);
}

static void runtime_publish_error_code(
    runtime *rt, const char *code, const char *msg)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_ERROR;
    if (code != NULL) {
        strncpy(event.data.error.code, code, sizeof(event.data.error.code) - 1);
    }
    if (msg != NULL) {
        strncpy(event.data.error.message, msg, sizeof(event.data.error.message) - 1);
    }
    runtime_publish_event(rt, &event);
}

static void runtime_publish_state_file_complete(
    runtime *rt,
    runtime_event_type type,
    const char *path)
{
    runtime_event event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    snprintf(event.data.state_file.path, sizeof(event.data.state_file.path), "%s", path ? path : "");
    runtime_publish_event(rt, &event);
}

static bool runtime_read_file_bytes(const char *path, uint8_t **out_bytes, size_t *out_size)
{
    FILE *file;
    long size;
    uint8_t *bytes;

    if (path == NULL || out_bytes == NULL || out_size == NULL) {
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes = (uint8_t *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(file);
        return false;
    }
    if (size > 0 && fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return false;
    }
    fclose(file);
    *out_bytes = bytes;
    *out_size = (size_t)size;
    return true;
}

static bool runtime_write_file_bytes(const char *path, const uint8_t *bytes, size_t size)
{
    FILE *file;

    if (path == NULL || (size > 0 && bytes == NULL)) {
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    if (size > 0 && fwrite(bytes, 1, size, file) != size) {
        fclose(file);
        return false;
    }
    if (ferror(file) != 0) {
        fclose(file);
        return false;
    }
    fclose(file);
    return true;
}

static bool runtime_at_instruction_boundary(const runtime *rt)
{
    return rt != NULL && !rt->machine.cpu.micro_active;
}

static void runtime_finish_to_instruction_boundary(runtime *rt)
{
    while (rt->machine.cpu.micro_active) {
        (void)apple2_step_cycle(&rt->machine);
    }
}

static bool runtime_finish_pending_state_snapshot_instruction(runtime *rt)
{
    size_t guard = 0;

    while (rt->machine.cpu.micro_active) {
        if (guard++ >= 4096u) {
            runtime_publish_error(rt, "failed to reach snapshot instruction boundary");
            return false;
        }
        if (!apple2_step_cycle(&rt->machine)) {
            runtime_publish_error(rt, "step_cycle failed during snapshot boundary");
            return false;
        }
    }
    return true;
}

static void runtime_save_state(runtime *rt, const runtime_command *command)
{
    const char *path = command->data.state_file.path;
    uint8_t *bytes;
    size_t size;
    size_t written;

    if (!runtime_finish_pending_state_snapshot_instruction(rt)) {
        return;
    }
    if (!apple2_snapshot_flush_media(&rt->machine)) {
        runtime_publish_error(rt, "failed to flush media before snapshot save");
        return;
    }
    size = apple2_snapshot_size(&rt->machine);
    if (size == 0) {
        runtime_publish_error(rt, "failed to size machine state snapshot");
        return;
    }
    bytes = (uint8_t *)malloc(size);
    if (bytes == NULL) {
        runtime_publish_error(rt, "failed to allocate machine state snapshot");
        return;
    }
    written = apple2_snapshot_save(&rt->machine, bytes, size);
    if (written != size) {
        free(bytes);
        runtime_publish_error(rt, "failed to serialize machine state snapshot");
        return;
    }
    if (!runtime_write_file_bytes(path, bytes, written)) {
        free(bytes);
        runtime_publish_error(rt, "failed to write machine state snapshot");
        return;
    }
    free(bytes);
    runtime_publish_state_file_complete(rt, RUNTIME_EVENT_SAVE_STATE_COMPLETE, path);
}

static void runtime_load_state(runtime *rt, const runtime_command *command)
{
    const char *path = command->data.state_file.path;
    uint8_t *bytes = NULL;
    size_t size = 0;
    bool was_running = rt->exec_state == RUNTIME_EXEC_RUNNING;
    runtime_stop_reason previous_stop_reason = rt->last_stop_reason;

    if (!runtime_read_file_bytes(path, &bytes, &size)) {
        runtime_publish_error(rt, "failed to read machine state snapshot");
        return;
    }
    if (!apple2_snapshot_load(&rt->machine, bytes, size)) {
        free(bytes);
        runtime_publish_error(rt, "failed to load machine state snapshot");
        return;
    }
    free(bytes);

    if (rt->history != NULL) {
        (void)runtime_history_clear_for_state_load(
            rt->history, apple2_cycles(&rt->machine));
    }
    runtime_inspector_on_history_invalidate(rt);
    runtime_frame_ring_clear(&rt->frame_ring);
    runtime_history_sync_observer(rt);
    apple2_paste_cancel(&rt->machine);

    rt->exec_state = was_running ? RUNTIME_EXEC_RUNNING : RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason =
        was_running ? RUNTIME_STOP_REASON_NONE : previous_stop_reason;
    if (was_running) {
        runtime_reset_pacer(rt);
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    } else {
        runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
    }
    runtime_publish_state_file_complete(rt, RUNTIME_EVENT_LOAD_STATE_COMPLETE, path);
    runtime_publish_cpu(rt, 0);
    runtime_publish_machine(rt);
    if (rt->machine.video.fb != NULL) {
        runtime_publish_canonical_frame(
            rt, RUNTIME_FRAME_PUBLISH_HOST_ONLY, 0u);
    }
}

static uint16_t runtime_read_main_word(runtime *rt, uint16_t address)
{
    view_flags_t main_view = view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);
    uint8_t lo = apple2_read_in_view(&rt->machine, main_view, address);
    uint8_t hi = apple2_read_in_view(&rt->machine, main_view, (uint16_t)(address + 1u));
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

static void runtime_write_main_word(runtime *rt, uint16_t address, uint16_t value)
{
    view_flags_t main_view = view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);
    apple2_write_in_view(&rt->machine, main_view, address, (uint8_t)value);
    apple2_write_in_view(&rt->machine, main_view, (uint16_t)(address + 1u), (uint8_t)(value >> 8));
}

static bool runtime_install_applesoft_program(
    runtime *rt,
    const uint8_t *program,
    size_t size)
{
    const uint16_t base = 0x0801u;
    uint16_t end;
    uint16_t memsiz;
    size_t i;
    view_flags_t main_view = view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);

    if (program == NULL || size < 2u || size > 0xffffu - base) return false;
    end = (uint16_t)(base + size);
    memsiz = runtime_read_main_word(rt, 0x0073u);
    /* Immediately after a reset ROM initialization may not yet have populated
       MEMSIZ. In that state use the standard top of 48K RAM; once Applesoft or
       DOS has established a credible HIMEM, respect its lower reservation. */
    if (memsiz < base + 2u || memsiz > 0xc000u) memsiz = 0xc000u;
    if (memsiz < end) return false;
    apple2_write_in_view(&rt->machine, main_view, 0x0800u, 0u);
    for (i = 0; i < size; ++i) {
        apple2_write_in_view(&rt->machine, main_view, (uint16_t)(base + i), program[i]);
    }
    runtime_write_main_word(rt, 0x0067u, base); /* TXTTAB */
    runtime_write_main_word(rt, 0x0069u, end);  /* VARTAB */
    runtime_write_main_word(rt, 0x006bu, end);  /* ARYTAB */
    runtime_write_main_word(rt, 0x006du, end);  /* STREND */
    runtime_write_main_word(rt, 0x006fu, memsiz); /* FRETOP */
    runtime_write_main_word(rt, 0x007du, 0x0800u); /* DATPTR: before first line */
    runtime_write_main_word(rt, 0x00afu, end);  /* PRGEND */
    runtime_write_main_word(rt, 0x00b8u, 0x0800u); /* TXTPTR: before first line */
    runtime_write_main_word(rt, 0x0075u, 0xffffu); /* CURLIN: immediate mode */
    apple2_write_in_view(&rt->machine, main_view, 0x0014u, 0u); /* SUBFLG */
    apple2_write_in_view(&rt->machine, main_view, 0x007au, 0u); /* OLDTEXT high */
    apple2_write_in_view(&rt->machine, main_view, 0x00d6u, 0u); /* LOCK */
    return true;
}

static void runtime_load_bin(runtime *rt, const runtime_command *command)
{
    const char *path = command->data.load_bin.path;
    uint8_t *file_bytes = NULL;
    size_t file_size = 0u;
    bool was_running = rt->exec_state == RUNTIME_EXEC_RUNNING;
    uint8_t *program = NULL;
    size_t program_size = 0u;
    apple2_binary_view binary;
    char error[160] = {0};

    if (!runtime_read_file_bytes(path, &file_bytes, &file_size)) {
        runtime_publish_error(rt, "failed to read load file");
        return;
    }
    if (command->data.load_bin.is_basic_text != 0u) {
        if (!apple2_applesoft_tokenize(
                file_bytes, file_size, &program, &program_size, error, sizeof(error))) {
            free(program);
            free(file_bytes);
            runtime_publish_error(rt, error[0] != '\0' ? error : "failed to tokenize Applesoft program");
            return;
        }
    } else {
        if (!apple2_binary_decode(
                path,
                file_bytes,
                file_size,
                (apple2_binary_format)command->data.load_bin.format,
                command->data.load_bin.address,
                &binary,
                error,
                sizeof(error))) {
            free(file_bytes);
            runtime_publish_error(rt, error[0] != '\0' ? error : "failed to decode binary");
            return;
        }
    }
    runtime_history_prepare_discontinuity(rt);
    if (command->data.load_bin.reset_first != 0u) {
        apple2_reset(&rt->machine);
        rt->suppress_execute_bp = false;
        rt->temp_bp_active = false;
        rt->breakpoint_hit_pending = false;
    }
    if (command->data.load_bin.is_basic_text != 0u) {
        if (!runtime_install_applesoft_program(rt, program, program_size)) {
            free(program);
            free(file_bytes);
            runtime_publish_error(rt, "Applesoft program exceeds current HIMEM");
            return;
        }
        free(program);
    } else {
        apple2_load(&rt->machine, binary.load_address, binary.data, binary.size);
        if (command->data.load_bin.run_after_load != 0u) {
            rt->machine.cpu.cpu.pc = binary.load_address;
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
            runtime_reset_pacer(rt);
        }
    }
    free(file_bytes);
    if (rt->history != NULL) {
        (void)runtime_history_transition_timeline(rt->history);
    }
    runtime_publish_cpu(rt, 0u);
    runtime_publish_machine(rt);
    if (command->data.load_bin.run_after_load != 0u && !was_running) {
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    }
}

static void runtime_save_bin(runtime *rt, const runtime_command *command)
{
    const char *path = command->data.save_bin.path;
    uint8_t *bytes = NULL;
    size_t size = 0u;
    char output_path[RUNTIME_COMMAND_PATH_MAX];

    snprintf(output_path, sizeof(output_path), "%s", path);
    if (command->data.save_bin.is_basic_text != 0u) {
        const uint16_t base = 0x0801u;
        uint16_t end = runtime_read_main_word(rt, 0x0069u);
        uint8_t *program;
        size_t i;
        char error[160] = {0};
        view_flags_t main_view = view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);
        if (runtime_read_main_word(rt, 0x0067u) != base || end <= base) {
            runtime_publish_error(rt, "no valid Applesoft program is present");
            return;
        }
        size = (size_t)(end - base);
        program = (uint8_t *)malloc(size);
        if (program == NULL) {
            runtime_publish_error(rt, "failed to allocate Applesoft program");
            return;
        }
        for (i = 0; i < size; ++i) {
            program[i] = apple2_read_in_view(&rt->machine, main_view, (uint16_t)(base + i));
        }
        if (!apple2_applesoft_detokenize(
                program, size, &bytes, &size, error, sizeof(error))) {
            free(program);
            runtime_publish_error(rt, error);
            return;
        }
        free(program);
    } else {
        uint16_t start = command->data.save_bin.start_address;
        uint16_t end = command->data.save_bin.end_address;
        size_t raw_size;
        uint8_t *raw;
        size_t i;
        apple2_binary_format format = (apple2_binary_format)command->data.save_bin.format;
        if (start > end) {
            runtime_publish_error(rt, "binary save start address exceeds end address");
            return;
        }
        raw_size = (size_t)end - start + 1u;
        raw = (uint8_t *)malloc(raw_size);
        if (raw == NULL) {
            runtime_publish_error(rt, "failed to allocate binary save buffer");
            return;
        }
        for (i = 0; i < raw_size; ++i) {
            raw[i] = apple2_debug_read(&rt->machine, (uint16_t)(start + i));
        }
        if (format == APPLE2_BINARY_FORMAT_APPLESINGLE) {
            if (!apple2_applesingle_encode_bin(raw, raw_size, start, &bytes, &size)) {
                free(raw);
                runtime_publish_error(rt, "failed to encode AppleSingle file");
                return;
            }
            free(raw);
        } else {
            bytes = raw;
            size = raw_size;
            if (format == APPLE2_BINARY_FORMAT_NAPS &&
                !apple2_naps_make_path(path, 0x06u, start, output_path, sizeof(output_path))) {
                free(bytes);
                runtime_publish_error(rt, "NAPS output filename is too long");
                return;
            }
        }
    }
    if (!runtime_write_file_bytes(output_path, bytes, size)) {
        free(bytes);
        runtime_publish_error(rt, "failed to write save file");
        return;
    }
    free(bytes);
}

static void runtime_fill_cpu_snapshot(runtime *rt, runtime_cpu_snapshot *out)
{
    CPU *cpu = &rt->machine.cpu.cpu;
    out->pc = cpu->pc;
    out->a = cpu->A;
    out->x = cpu->X;
    out->y = cpu->Y;
    out->sp = (uint8_t)(cpu->sp & 0xFFu);
    out->p = cpu->flags;
    out->cycles = cpu->cycles;
}

static void runtime_publish_cpu(runtime *rt, uint64_t token)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_CPU_STATE_RESPONSE;
    event.request_token = token;
    runtime_fill_cpu_snapshot(rt, &event.data.cpu_state);
    runtime_publish_event(rt, &event);
}

static void runtime_publish_call_stack(runtime *rt, uint64_t token)
{
    runtime_event event;
    apple2_call_stack_entry entries[APPLE2_CALL_STACK_MAX];
    uint8_t count;
    uint8_t i;
    uint8_t max_entries;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_CALL_STACK_RESPONSE;
    event.request_token = token;
    event.data.call_stack.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xFFu);

    max_entries = (uint8_t)RUNTIME_CALL_STACK_MAX;
    if (max_entries > (uint8_t)APPLE2_CALL_STACK_MAX) {
        max_entries = (uint8_t)APPLE2_CALL_STACK_MAX;
    }
    count = apple2_debug_call_stack(&rt->machine, entries, max_entries);
    event.data.call_stack.count = count;
    for (i = 0; i < count; i++) {
        event.data.call_stack.entries[i].jsr_address = entries[i].jsr_address;
        event.data.call_stack.entries[i].dest_address = entries[i].dest_address;
    }
    runtime_publish_event(rt, &event);
}

static void runtime_publish_machine(runtime *rt)
{
    runtime_event event;
    int slot;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_MACHINE_STATE_RESPONSE;
    event.data.machine_state.runtime_seq = ++rt->runtime_seq;
    event.data.machine_state.cycle = apple2_cycles(&rt->machine);
    event.data.machine_state.cpu_cycles = apple2_cycles(&rt->machine);
    event.data.machine_state.pc = rt->machine.cpu.cpu.pc;
    event.data.machine_state.a = rt->machine.cpu.cpu.A;
    event.data.machine_state.x = rt->machine.cpu.cpu.X;
    event.data.machine_state.y = rt->machine.cpu.cpu.Y;
    event.data.machine_state.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xFFu);
    event.data.machine_state.p = rt->machine.cpu.cpu.flags;
    event.data.machine_state.ready = rt->machine.ready ? 1u : 0u;
    event.data.machine_state.running = rt->exec_state == RUNTIME_EXEC_RUNNING ? 1u : 0u;
    event.data.machine_state.stop_reason = rt->last_stop_reason;
    event.data.machine_state.frame_number = rt->machine.video.frame_number;
    event.data.machine_state.dropped_frames = rt->frame_slot.dropped_frames;
    event.data.machine_state.active_turbo_multiplier = rt->active_turbo_multiplier;
    event.data.machine_state.turbo_speed_count = rt->turbo_speed_count;
    event.data.machine_state.apple_state_flags = rt->machine.state_flags;
    event.data.machine_state.apple_model =
        rt->machine.model == APPLE2_MODEL_II_PLUS ? 1u : 0u;
    event.data.machine_state.video_line = rt->machine.video.line;
    event.data.machine_state.video_cycle_in_line = rt->machine.video.cycle_in_line;
    event.data.machine_state.inspector_mode = (uint8_t)runtime_inspector_current_mode(rt);
    event.data.machine_state.inspector_enabled = runtime_inspector_enabled(rt) ? 1u : 0u;
    event.data.machine_state.inspector_stopped_for_max =
        (rt->history_paused_for_max ||
         (runtime_turbo_is_max_value(rt->active_turbo_multiplier) &&
          rt->history_off_on_max)) ? 1u : 0u;
    if (rt->history != NULL) {
        runtime_history_status st;
        runtime_history_get_status(rt->history, &st);
        event.data.machine_state.inspector_history_recording =
            (st.available && st.recording) ? 1u : 0u;
    }
    {
        runtime_frame_ring_info fi;
        runtime_frame_ring_get_info(&rt->frame_ring, &fi);
        event.data.machine_state.inspector_frame_recording = fi.recording ? 1u : 0u;
    }
    event.data.machine_state.inspector_recorder_recording =
        runtime_inspector_recorder_is_recording(rt) ? 1u : 0u;
    event.data.machine_state.inspector_focus_cycle =
        rt->machine_ready ? apple2_cycles(&rt->machine) : 0u;
    event.data.machine_state.inspector_focus_id = 0u;
    {
        uint64_t oldest = 0u;
        uint64_t live = 0u;
        uint64_t count = 0u;
        runtime_inspector_window extras;

        runtime_inspector_timeline_bounds(rt, &oldest, &live, &count);
        memset(&extras, 0, sizeof(extras));
        runtime_inspector_fill_window_extras(rt, &extras);
        event.data.machine_state.inspector_window_valid = count > 0u ? 1u : 0u;
        event.data.machine_state.inspector_window_start_kind = (uint8_t)extras.start_kind;
        event.data.machine_state.inspector_window_start_arg1 = extras.start_arg1;
        event.data.machine_state.inspector_oldest_cycle = oldest;
        event.data.machine_state.inspector_newest_cycle = live;
        event.data.machine_state.inspector_oldest_id = 0u;
        event.data.machine_state.inspector_newest_id = 0u;
    }
    for (slot = 1; slot <= 7; ++slot) {
        runtime_slot_snapshot *out = &event.data.machine_state.slots[slot];
        int device;
        switch (rt->machine.slot_type[slot]) {
        case SLOT_TYPE_DISKII:
            out->card_type = RUNTIME_SLOT_CARD_DISKII;
            break;
        case SLOT_TYPE_SMARTPORT:
            out->card_type = RUNTIME_SLOT_CARD_SMARTPORT;
            break;
        case SLOT_TYPE_MOCKINGBOARD:
            out->card_type = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            break;
        case SLOT_TYPE_EMPTY:
        default:
            out->card_type = RUNTIME_SLOT_CARD_EMPTY;
            break;
        }
        for (device = 0; device < 2; ++device) {
            runtime_media_device_snapshot *media = &out->devices[device];
            if (out->card_type == RUNTIME_SLOT_CARD_DISKII) {
                DISKII_DRIVE *drive =
                    &rt->machine.diskii_controller[slot].diskii_drive[device];
                media->queue_count = (uint16_t)drive->images.items;
                media->queue_index = drive->image_index >= 0 ?
                    (uint16_t)drive->image_index : 0u;
                media->mounted = drive->active_image != NULL ? 1u : 0u;
                media->writable = drive->sensor_protect ? 0u : 1u;
                media->dirty = drive->active_image != NULL &&
                    image_is_dirty(drive->active_image) ? 1u : 0u;
                if (drive->active_image != NULL &&
                    drive->active_image->file.file_display_name != NULL) {
                    snprintf(media->display_name, sizeof(media->display_name), "%s",
                        drive->active_image->file.file_display_name);
                }
            } else if (out->card_type == RUNTIME_SLOT_CARD_SMARTPORT) {
                SP_DEVICE *spd = &rt->machine.sp_device[slot];
                const char *display;
                media->mounted = sp_unit_mounted(spd, device) ? 1u : 0u;
                media->writable = sp_unit_mounted(spd, device) ? 1u : 0u;
                media->queue_count = media->mounted;
                display = sp_unit_display_name(spd, device);
                if (display != NULL) {
                    snprintf(media->display_name, sizeof(media->display_name), "%s",
                        display);
                }
            }
        }
    }
    {
        uint8_t mask = 0;
        int s;
        for (s = 1; s <= 7; s++) {
            if (rt->machine.diskii_present[s] &&
                (rt->machine.diskii_controller[s].diskii_drive[0].motor_on ||
                 rt->machine.diskii_controller[s].diskii_drive[1].motor_on)) {
                mask = (uint8_t)(mask | (1u << s));
            }
        }
        event.data.machine_state.disk_motor_mask = mask;
    }
    runtime_publish_event(rt, &event);
}

static void runtime_refresh_rw_breakpoint_flag(runtime *rt);

static void runtime_publish_breakpoints(runtime *rt)
{
    runtime_event event;
    size_t i;
    size_t n;
    runtime_breakpoint_snapshot snap;

    runtime_refresh_rw_breakpoint_flag(rt);

    memset(&snap, 0, sizeof(snap));
    n = rt->breakpoint_count;
    if (n > RUNTIME_BREAKPOINT_SNAPSHOT_MAX) {
        n = RUNTIME_BREAKPOINT_SNAPSHOT_MAX;
    }
    snap.count = (uint16_t)n;
    for (i = 0; i < n; i++) {
        runtime_breakpoint_snapshot_entry *e = &snap.entries[i];
        e->id = rt->breakpoints[i].id;
        e->start_address = rt->breakpoints[i].start_address;
        e->end_address = rt->breakpoints[i].end_address;
        e->has_end_address = rt->breakpoints[i].has_end_address ? 1u : 0u;
        e->access = (runtime_breakpoint_access)rt->breakpoints[i].access_mask;
        e->mapping = rt->breakpoints[i].mapping;
        e->actions = rt->breakpoints[i].action_mask;
        e->enabled = rt->breakpoints[i].enabled ? 1u : 0u;
        e->use_counter = rt->breakpoints[i].use_counter ? 1u : 0u;
        e->current_hits = rt->breakpoints[i].current_hits;
        e->initial_count = rt->breakpoints[i].initial_count;
        e->reset_count = rt->breakpoints[i].reset_count;
        e->counter = rt->breakpoints[i].counter;
        e->swap_slot = rt->breakpoints[i].swap_slot;
        e->swap_param = rt->breakpoints[i].swap_param;
        e->swap_relative = rt->breakpoints[i].swap_relative;
        snprintf(e->tron_path, sizeof(e->tron_path), "%s", rt->breakpoints[i].tron_path);
        snprintf(e->type_text, sizeof(e->type_text), "%s", rt->breakpoints[i].type_text);
        e->condition = rt->breakpoints[i].condition;
        e->address = rt->breakpoints[i].start_address;
        e->target_hits = rt->breakpoints[i].initial_count;
    }
    if (rt->breakpoint_slot.mutex != NULL) {
        mutex_lock(rt->breakpoint_slot.mutex);
        rt->breakpoint_slot.snapshot = snap;
        rt->breakpoint_slot.has_snapshot = true;
        mutex_unlock(rt->breakpoint_slot.mutex);
    }
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_BREAKPOINTS_RESPONSE;
    event.data.breakpoints = snap;
    event.data.breakpoints_ready.count = (uint16_t)n;
    runtime_publish_event(rt, &event);
}

static int runtime_find_breakpoint_by_id(const runtime *rt, uint32_t id)
{
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        if (rt->breakpoints[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool runtime_breakpoint_mapping_is_valid(runtime_breakpoint_mapping mapping)
{
    return (mapping & ~(A2SEL_48K_MASK | A2SEL_C100_MASK | A2SEL_D000_MASK)) == 0u &&
        vf_get_ram(mapping) <= A2SEL48K_AUX &&
        vf_get_c100(mapping) <= A2SELC100_ROM &&
        vf_get_d000(mapping) <= A2SELD000_ROM;
}

static bool runtime_breakpoint_mapping_is_map(runtime_breakpoint_mapping mapping)
{
    return mapping == 0u;
}

static bool runtime_breakpoint_definition_is_valid(const runtime_breakpoint_definition *definition)
{
    uint32_t supported_access =
        RUNTIME_BREAKPOINT_ACCESS_EXECUTE |
        RUNTIME_BREAKPOINT_ACCESS_READ |
        RUNTIME_BREAKPOINT_ACCESS_WRITE;
    uint32_t supported_actions =
        RUNTIME_BREAKPOINT_ACTION_BREAK |
        RUNTIME_BREAKPOINT_ACTION_FAST |
        RUNTIME_BREAKPOINT_ACTION_SLOW |
        RUNTIME_BREAKPOINT_ACTION_TRON |
        RUNTIME_BREAKPOINT_ACTION_TROFF |
        RUNTIME_BREAKPOINT_ACTION_TYPE |
        RUNTIME_BREAKPOINT_ACTION_SWAP;

    if (definition == NULL) {
        return false;
    }
    if ((definition->access & supported_access) == 0 ||
        (definition->access & ~supported_access) != 0) {
        return false;
    }
    if (!runtime_breakpoint_mapping_is_valid(definition->mapping)) {
        return false;
    }
    if ((definition->actions & ~supported_actions) != 0) {
        return false;
    }
    /* value= has no meaning on instruction fetch. */
    if ((definition->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 &&
        runtime_bp_condition_uses_value(&definition->condition)) {
        return false;
    }
    if (definition->condition.term_count > 0u &&
        !runtime_bp_condition_is_valid(&definition->condition)) {
        return false;
    }
    if ((definition->actions & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0 &&
        definition->swap_slot > 7u) {
        return false;
    }
    return true;
}

static void runtime_breakpoint_apply_definition(
    runtime_breakpoint *breakpoint,
    const runtime_breakpoint_definition *definition,
    bool reset_hits)
{
    breakpoint->enabled = definition->enabled != 0;
    breakpoint->start_address = definition->start_address;
    breakpoint->end_address = definition->has_end_address ?
        definition->end_address :
        definition->start_address;
    breakpoint->has_end_address = definition->has_end_address != 0;
    breakpoint->access_mask = definition->access;
    breakpoint->mapping = definition->mapping;
    breakpoint->action_mask = definition->actions;
    breakpoint->use_counter = definition->use_counter != 0;
    breakpoint->initial_count = definition->initial_count;
    breakpoint->reset_count = definition->reset_count;
    breakpoint->counter = definition->initial_count;
    if (reset_hits) {
        breakpoint->current_hits = 0;
    }
    breakpoint->swap_slot = definition->swap_slot;
    breakpoint->swap_param = definition->swap_param;
    breakpoint->swap_relative = definition->swap_relative;
    snprintf(breakpoint->tron_path, sizeof(breakpoint->tron_path), "%s", definition->tron_path);
    snprintf(breakpoint->type_text, sizeof(breakpoint->type_text), "%s", definition->type_text);
    breakpoint->condition = definition->condition;
    if (!runtime_bp_condition_is_valid(&breakpoint->condition)) {
        memset(&breakpoint->condition, 0, sizeof(breakpoint->condition));
    }
}

static bool runtime_add_breakpoint(
    runtime *rt,
    const runtime_breakpoint_definition *definition,
    uint32_t *out_id)
{
    runtime_breakpoint *breakpoint;

    if (!runtime_breakpoint_definition_is_valid(definition)) {
        runtime_publish_error(rt, "invalid breakpoint definition");
        runtime_publish_breakpoints(rt);
        return false;
    }
    if (rt->breakpoint_count >= RUNTIME_BREAKPOINT_CAPACITY) {
        runtime_publish_error(rt, "breakpoint table is full");
        runtime_publish_breakpoints(rt);
        return false;
    }
    if (rt->next_breakpoint_id == 0u) {
        rt->next_breakpoint_id = 1u;
    }

    breakpoint = &rt->breakpoints[rt->breakpoint_count];
    memset(breakpoint, 0, sizeof(*breakpoint));
    breakpoint->id = rt->next_breakpoint_id++;
    runtime_breakpoint_apply_definition(breakpoint, definition, true);
    rt->breakpoint_count++;

    if (out_id != NULL) {
        *out_id = breakpoint->id;
    }
    runtime_publish_breakpoints(rt);
    return true;
}

static int runtime_find_execute_breakpoint_by_address(const runtime *rt, uint16_t address)
{
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        if (rt->breakpoints[i].start_address == address &&
            !rt->breakpoints[i].has_end_address &&
            (rt->breakpoints[i].access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 &&
            runtime_breakpoint_mapping_is_map(rt->breakpoints[i].mapping) &&
            (rt->breakpoints[i].action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool runtime_breakpoint_address_matches(
    const runtime_breakpoint *breakpoint,
    uint16_t address)
{
    uint16_t lo;
    uint16_t hi;

    if (!breakpoint->has_end_address) {
        return breakpoint->start_address == address;
    }
    /*
     * Linear range only. c64m allowed wrap when start > end (matches almost the
     * whole map); that made UI ranges like C800..C7FE fire on every instruction.
     * Invert → treat as min..max.
     */
    lo = breakpoint->start_address;
    hi = breakpoint->end_address;
    if (lo > hi) {
        uint16_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    return address >= lo && address <= hi;
}

/*
 * Classify the currently mapped page host pointer for this CPU address.
 * LC layout (softswitch map_lc): D000 bank1 @0 / bank2 @0x1000; E000 @0x2000;
 * ALTZP LC uses +0x4000. E000 is shared by both LC banks.
 */
static bool runtime_breakpoint_mapping_matches(
    runtime *rt,
    const runtime_breakpoint *breakpoint,
    runtime_breakpoint_access access,
    uint16_t address)
{
    uint16_t page;
    uint8_t *ptr;
    apple2_t *m;

    m = &rt->machine;
    if (address >= 0xC000u && address < 0xC100u) {
        return true;
    }
    page = (uint16_t)(address / APPLE2_PAGE_SIZE);
    if (m->pages.read_pages == NULL || m->pages.write_pages == NULL ||
        page >= APPLE2_NUM_PAGES) {
        return false;
    }
    ptr = access == RUNTIME_BREAKPOINT_ACCESS_WRITE ?
        m->pages.write_pages[page] : m->pages.read_pages[page];
    if (ptr == NULL) {
        return false;
    }

    if (address < 0xC000u) {
        if (vf_get_ram(breakpoint->mapping) == A2SEL48K_MAPPED) {
            return true;
        }
        if (m->ram_main == NULL || ptr < m->ram_main ||
            ptr >= m->ram_main + APPLE2_RAM_MAIN_SIZE) {
            return false;
        }
        if (vf_get_ram(breakpoint->mapping) == A2SEL48K_MAIN) {
            return ptr < m->ram_main + 0x10000u;
        }
        return ptr >= m->ram_main + 0x10000u;
    }

    if (address < 0xD000u) {
        if (vf_get_c100(breakpoint->mapping) == A2SELC100_MAPPED) {
            return true;
        }
        return m->rom_c000 != NULL && m->rom_c000_size > 0u &&
            m->pages.read_pages[page] >= m->rom_c000 &&
            m->pages.read_pages[page] < m->rom_c000 + m->rom_c000_size;
    }

    if (vf_get_d000(breakpoint->mapping) == A2SELD000_MAPPED) {
        return true;
    }
    if (vf_get_d000(breakpoint->mapping) == A2SELD000_LC_B1 ||
        vf_get_d000(breakpoint->mapping) == A2SELD000_LC_B2) {
        size_t off;
        bool d000_b1;
        bool d000_b2;
        bool e000_shared;
        bool aux;

        if (m->ram_lc == NULL ||
            ptr < m->ram_lc ||
            ptr >= m->ram_lc + APPLE2_RAM_LC_SIZE) {
            return false;
        }
        off = (size_t)(ptr - m->ram_lc);
        d000_b1 = (off < 0x1000u) || (off >= 0x4000u && off < 0x5000u);
        d000_b2 = (off >= 0x1000u && off < 0x2000u) ||
            (off >= 0x5000u && off < 0x6000u);
        e000_shared = (off >= 0x2000u && off < 0x4000u) ||
            (off >= 0x6000u && off < 0x8000u);
        aux = off >= 0x4000u;
        if (vf_get_ram(breakpoint->mapping) == A2SEL48K_MAIN && aux) {
            return false;
        }
        if (vf_get_ram(breakpoint->mapping) == A2SEL48K_AUX && !aux) {
            return false;
        }
        if (vf_get_d000(breakpoint->mapping) == A2SELD000_LC_B1) {
            return d000_b1 || e000_shared;
        }
        return d000_b2 || e000_shared;
    }

    return (m->rom_d000 != NULL && m->rom_d000_size > 0u &&
            ptr >= m->rom_d000 && ptr < m->rom_d000 + m->rom_d000_size) ||
        (access == RUNTIME_BREAKPOINT_ACCESS_WRITE && m->rom_sink != NULL &&
            ptr >= m->rom_sink && ptr < m->rom_sink + 0x3000u);
}

static bool runtime_breakpoint_record_match(runtime *rt, runtime_breakpoint *breakpoint)
{
    breakpoint->current_hits++;

    if (!breakpoint->use_counter) {
        return true;
    }
    if (breakpoint->counter > 0u) {
        breakpoint->counter--;
    }
    if (breakpoint->counter > 0u) {
        return false;
    }
    if (breakpoint->reset_count == 0u) {
        breakpoint->enabled = false;
        runtime_publish_breakpoints(rt);
        return true;
    }
    breakpoint->counter = breakpoint->reset_count;
    return true;
}

static bool runtime_execute_breakpoint_actions(runtime *rt, const runtime_breakpoint *breakpoint)
{
    bool turbo_changed = false;

    /*
     * FAST -> max free-run; SLOW -> 1 MHz (real-time).
     * If both bits are set, apply FAST then SLOW so SLOW wins.
     */
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_FAST) != 0) {
        if (rt->active_turbo_multiplier != RUNTIME_TURBO_MAX) {
            runtime_set_active_turbo(rt, RUNTIME_TURBO_MAX);
            turbo_changed = true;
        }
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0) {
        if (rt->active_turbo_multiplier != RUNTIME_TURBO_MHZ_1) {
            runtime_set_active_turbo(rt, RUNTIME_TURBO_MHZ_1);
            turbo_changed = true;
        }
    }
    if (turbo_changed) {
        runtime_publish_machine(rt);
    }

    /*
     * TYPE: Apple script (plain text + \[…] escapes). Clipboard Opt+Insert stays
     * plain apple2_paste_begin only — see runtime_type_script_*.
     */
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0 &&
        breakpoint->type_text[0] != '\0') {
        runtime_type_script_start(rt, breakpoint->type_text);
    }

    /* TRON/TROFF: append instruction lines to a host file while enabled. */
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TRON) != 0) {
        rt->trace_enabled = true;
        if (rt->trace_file == NULL) {
            const char *path = (breakpoint->tron_path[0] != '\0') ?
                breakpoint->tron_path :
                "trace.log";
            rt->trace_file = fopen(path, "a");
            if (rt->trace_file != NULL) {
                fprintf(
                    rt->trace_file,
                    "--- TRON  CYC=%08llX ---\n",
                    (unsigned long long)apple2_cycles(&rt->machine));
            }
        }
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0) {
        rt->trace_enabled = false;
        if (rt->trace_file != NULL) {
            fprintf(
                rt->trace_file,
                "--- TROFF CYC=%08llX ---\n",
                (unsigned long long)apple2_cycles(&rt->machine));
            fclose(rt->trace_file);
            rt->trace_file = NULL;
        }
    }

    /*
     * SWAP: step a multi-image queue on Disk II (drive 0).
     * Machine owns the live queue; host may mirror current index via DISK_SWAP.
     * Bare SWAP / param 0 → next image (relative +1).
     */
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0) {
        const int slot = breakpoint->swap_slot;
        const int drive = 0;
        int32_t param = breakpoint->swap_param;
        uint8_t relative = breakpoint->swap_relative;
        runtime_event ev;

        if (param == 0) {
            param = 1;
            relative = 1u;
        }
        if (slot < 1 || slot > 7 || !rt->machine.diskii_present[slot]) {
            char message[96];
            snprintf(message, sizeof(message),
                "breakpoint SWAP slot %d does not contain a Disk II", slot);
            runtime_publish_error(rt, message);
            return true;
        }
        (void)apple2_disk_swap(
            &rt->machine,
            slot,
            drive,
            param,
            relative != 0u);
        memset(&ev, 0, sizeof(ev));
        ev.type = RUNTIME_EVENT_DISK_SWAP;
        ev.data.disk_swap.slot = (uint8_t)slot;
        ev.data.disk_swap.swap_param = param;
        ev.data.disk_swap.swap_relative = relative;
        ev.data.disk_swap.device = (uint8_t)drive; /* Apple drive 0/1 */
        runtime_publish_event(rt, &ev);
    }

    /* BREAK: pause after side effects. Actions may combine with BREAK. */
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
        return true;
    }
    return false;
}

static uint8_t runtime_breakpoint_condition_read(void *user, uint16_t address)
{
    return apple2_debug_read((const apple2_t *)user, address);
}

static bool runtime_breakpoint_condition_matches(
    runtime *rt,
    const runtime_breakpoint *breakpoint,
    bool has_value,
    uint8_t value)
{
    runtime_bp_eval_context context;

    if (breakpoint->condition.term_count == 0u) {
        return true;
    }

    memset(&context, 0, sizeof(context));
    context.a = rt->machine.cpu.cpu.A;
    context.x = rt->machine.cpu.cpu.X;
    context.y = rt->machine.cpu.cpu.Y;
    context.sp = (uint8_t)(rt->machine.cpu.cpu.sp & 0xffu);
    context.p = rt->machine.cpu.cpu.flags;
    context.has_value = has_value;
    context.value = value;
    context.raster = rt->machine.video.line;
    context.cycle_in_line = rt->machine.video.cycle_in_line;
    context.mem_read = runtime_breakpoint_condition_read;
    context.mem_read_user = &rt->machine;

    return runtime_bp_condition_eval(&breakpoint->condition, &context);
}

/* Shared exec/R/W match. R/W path: bus callback → matches_access → hit_pending. */
static bool runtime_breakpoint_matches_access(
    runtime *rt,
    runtime_breakpoint_access access,
    uint16_t address,
    bool has_value,
    uint8_t value)
{
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        runtime_breakpoint *breakpoint = &rt->breakpoints[i];

        if (breakpoint->enabled &&
            (breakpoint->access_mask & access) != 0 &&
            runtime_breakpoint_address_matches(breakpoint, address) &&
            runtime_breakpoint_mapping_matches(rt, breakpoint, access, address) &&
            runtime_breakpoint_condition_matches(rt, breakpoint, has_value, value) &&
            runtime_breakpoint_record_match(rt, breakpoint)) {
            return runtime_execute_breakpoint_actions(rt, breakpoint);
        }
    }
    return false;
}

static bool runtime_breakpoint_matches_pc(runtime *rt)
{
    uint16_t pc;

    if (!runtime_at_instruction_boundary(rt)) {
        return false;
    }

    pc = rt->machine.cpu.cpu.pc;
    if (rt->temp_bp_active && rt->temp_bp_address == pc) {
        if (rt->temp_bp_skip_current) {
            return false;
        }
        return true;
    }

    return runtime_breakpoint_matches_access(
        rt,
        RUNTIME_BREAKPOINT_ACCESS_EXECUTE,
        pc,
        false,
        0u);
}

static void runtime_set_execute_breakpoint(runtime *rt, const runtime_command *command)
{
    runtime_breakpoint_definition definition;
    int index = runtime_find_execute_breakpoint_by_address(
        rt,
        command->data.set_execute_breakpoint.address);

    if (index >= 0) {
        rt->breakpoints[index].enabled = command->data.set_execute_breakpoint.enabled != 0;
        runtime_publish_breakpoints(rt);
        return;
    }

    memset(&definition, 0, sizeof(definition));
    definition.enabled = command->data.set_execute_breakpoint.enabled;
    definition.start_address = command->data.set_execute_breakpoint.address;
    definition.end_address = command->data.set_execute_breakpoint.address;
    definition.has_end_address = 0;
    definition.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition.mapping = 0u;
    definition.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    (void)runtime_add_breakpoint(rt, &definition, NULL);
}

static void runtime_create_breakpoint(runtime *rt, const runtime_command *command)
{
    (void)runtime_add_breakpoint(rt, &command->data.create_breakpoint.definition, NULL);
}

static void runtime_update_breakpoint(runtime *rt, const runtime_command *command)
{
    int index = runtime_find_breakpoint_by_id(rt, command->data.update_breakpoint.id);

    if (index < 0) {
        runtime_publish_error(rt, "breakpoint id not found");
        runtime_publish_breakpoints(rt);
        return;
    }
    if (!runtime_breakpoint_definition_is_valid(&command->data.update_breakpoint.definition)) {
        runtime_publish_error(rt, "invalid breakpoint definition");
        runtime_publish_breakpoints(rt);
        return;
    }
    runtime_breakpoint_apply_definition(
        &rt->breakpoints[index],
        &command->data.update_breakpoint.definition,
        true);
    runtime_publish_breakpoints(rt);
}

static void runtime_duplicate_breakpoint(runtime *rt, const runtime_command *command)
{
    runtime_breakpoint_definition definition;
    runtime_breakpoint *source;
    int index = runtime_find_breakpoint_by_id(rt, command->data.duplicate_breakpoint.id);

    if (index < 0) {
        runtime_publish_error(rt, "breakpoint id not found");
        runtime_publish_breakpoints(rt);
        return;
    }

    source = &rt->breakpoints[index];
    memset(&definition, 0, sizeof(definition));
    definition.enabled = source->enabled ? 1u : 0u;
    definition.start_address = source->start_address;
    definition.end_address = source->end_address;
    definition.has_end_address = source->has_end_address ? 1u : 0u;
    definition.access = source->access_mask;
    definition.mapping = source->mapping;
    definition.actions = source->action_mask;
    definition.use_counter = source->use_counter ? 1u : 0u;
    definition.initial_count = source->initial_count;
    definition.reset_count = source->reset_count;
    definition.swap_slot = source->swap_slot;
    definition.swap_param = source->swap_param;
    definition.swap_relative = source->swap_relative;
    snprintf(definition.tron_path, sizeof(definition.tron_path), "%s", source->tron_path);
    snprintf(definition.type_text, sizeof(definition.type_text), "%s", source->type_text);
    definition.condition = source->condition;
    (void)runtime_add_breakpoint(rt, &definition, NULL);
}

static void runtime_clear_breakpoint(runtime *rt, const runtime_command *command)
{
    int index = runtime_find_breakpoint_by_id(rt, command->data.clear_breakpoint.id);

    if (index >= 0) {
        size_t i;
        for (i = (size_t)index; i + 1u < rt->breakpoint_count; ++i) {
            rt->breakpoints[i] = rt->breakpoints[i + 1u];
        }
        rt->breakpoint_count--;
    }
    runtime_publish_breakpoints(rt);
}

static void runtime_clear_all_breakpoints(runtime *rt)
{
    rt->breakpoint_count = 0;
    runtime_publish_breakpoints(rt);
}

static void runtime_set_breakpoint_enabled(runtime *rt, const runtime_command *command)
{
    int index = runtime_find_breakpoint_by_id(rt, command->data.set_breakpoint_enabled.id);

    if (index >= 0) {
        rt->breakpoints[index].enabled = command->data.set_breakpoint_enabled.enabled != 0;
    }
    runtime_publish_breakpoints(rt);
}

static void runtime_rearm_oneshot_breakpoints(runtime *rt)
{
    size_t i;

    for (i = 0; i < rt->breakpoint_count; ++i) {
        runtime_breakpoint *bp = &rt->breakpoints[i];
        if (bp->use_counter && bp->reset_count == 0u && !bp->enabled) {
            bp->enabled = true;
            bp->counter = bp->initial_count;
        }
    }
    runtime_publish_breakpoints(rt);
}

static void runtime_refresh_rw_breakpoint_flag(runtime *rt)
{
    size_t i;
    rt->has_rw_breakpoints = false;
    for (i = 0; i < rt->breakpoint_count; ++i) {
        if (rt->breakpoints[i].enabled &&
            (rt->breakpoints[i].access_mask &
             (RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE)) != 0) {
            rt->has_rw_breakpoints = true;
            return;
        }
    }
}

static void runtime_on_memory_access(
    void *user,
    apple2_memory_access_type access,
    uint16_t address,
    uint8_t value)
{
    runtime *rt = (runtime *)user;
    runtime_breakpoint_access bp_access;

    if (rt == NULL || rt->breakpoint_hit_pending || !rt->has_rw_breakpoints) {
        return;
    }

    bp_access = access == APPLE2_MEMORY_ACCESS_WRITE ?
        RUNTIME_BREAKPOINT_ACCESS_WRITE :
        RUNTIME_BREAKPOINT_ACCESS_READ;

    if (runtime_breakpoint_matches_access(rt, bp_access, address, true, value)) {
        rt->breakpoint_hit_pending = true;
    }
}

static void runtime_pause_for_breakpoint(runtime *rt)
{
    rt->breakpoint_hit_pending = false;
    rt->suppress_execute_bp = true;
    rt->temp_bp_active = false;
    rt->temp_bp_skip_current = false;
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_BREAKPOINT;
    /* Machine state before PAUSED so control can report stop=breakpoint. */
    runtime_publish_machine(rt);
    runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_cpu(rt, 0u);
    runtime_publish_breakpoints(rt);
    runtime_publish_presented_frame(rt);
    if (rt->inspecting) {
        rt->machine.video.paint_enabled = true;
    }
}

static bool runtime_pause_if_breakpoint_pending(runtime *rt)
{
    if (rt == NULL || !rt->breakpoint_hit_pending) {
        return false;
    }
    runtime_pause_for_breakpoint(rt);
    return true;
}

static void runtime_pause_for_step(runtime *rt)
{
    runtime_event event;

    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_STEP;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_STEP_COMPLETE;
    event.data.step_complete.reason = RUNTIME_STOP_REASON_STEP;
    runtime_fill_cpu_snapshot(rt, &event.data.step_complete.cpu);
    runtime_publish_event(rt, &event);

    runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_cpu(rt, 0u);
    runtime_publish_presented_frame(rt);
    if (rt->inspecting) {
        rt->machine.video.paint_enabled = true;
    }
}

static bool runtime_turbo_is_free_run(const runtime *rt)
{
    /* Max (0) free-runs; finite milli-MHz values are paced. */
    return runtime_turbo_is_max_value(rt->active_turbo_multiplier);
}

/*
 * Max (S2): video offline (no beam); presentation paint on wall quanta.
 * Finite: beam paint on. Leaving max reseeds beam from total Φ0.
 */
static void runtime_apply_turbo_video_policy(runtime *rt, bool leaving_max)
{
    if (rt == NULL || !rt->machine_ready) {
        return;
    }
    if (runtime_turbo_is_free_run(rt)) {
        rt->machine.video.paint_enabled = false;
        rt->block_paint_initialized = false;
    } else {
        if (leaving_max) {
            apple2_video_reseed_from_cycles(&rt->machine);
        }
        rt->machine.video.paint_enabled = true;
    }
}

static void runtime_inspector_reattach_live_hooks(runtime *rt);

/* Pause/resume flight-recorder around max free-run (history_off_on_max policy).
   TMA3: entering max remembers Record, wipes the TM tape, and turns Record
   off. Leaving max restores Record (fresh tape) if it was on. */
static void runtime_history_apply_max_policy(runtime *rt, bool entering_max, bool leaving_max)
{
    runtime_history_status st;
    uint64_t cycle;

    if (rt == NULL || !rt->history_off_on_max) {
        return;
    }
    cycle = rt->machine_ready ? apple2_cycles(&rt->machine) : 0u;

    if (entering_max) {
        if (rt->inspecting) {
            runtime_inspector_leave(rt);
            runtime_inspector_reattach_live_hooks(rt);
        }
        rt->inspector_enabled_saved_for_max = rt->inspector_enabled;
        if (rt->history != NULL) {
            runtime_history_get_status(rt->history, &st);
            if (st.available && st.recording) {
                (void)runtime_history_stop(rt->history, cycle);
                rt->history_paused_for_max = true;
                runtime_history_sync_observer(rt);
            }
        }
        runtime_inspector_recorder_set_enabled(rt, false);
        runtime_inspector_on_history_invalidate(rt);
        if (rt->inspector_enabled_saved_for_max) {
            runtime_frame_ring_set_recording(&rt->frame_ring, false);
            runtime_frame_ring_clear(&rt->frame_ring);
        }
        rt->inspector_enabled = false;
    } else if (leaving_max) {
        if (rt->history_paused_for_max && rt->history != NULL) {
            (void)runtime_history_resume(rt->history, cycle);
            rt->history_paused_for_max = false;
            runtime_history_sync_observer(rt);
        }
        if (rt->inspector_enabled_saved_for_max) {
            rt->inspector_enabled_saved_for_max = false;
            runtime_inspector_set_enabled(rt, true);
        }
    }
}

static void runtime_set_active_turbo(runtime *rt, uint32_t milli_mhz);

/* Install a Configure/OK turbo ladder. Keep the current speed if it is still
   on the list; otherwise switch to the first entry (enter/leave max correctly). */
static void runtime_install_turbo_ladder(
    runtime *rt,
    const uint32_t *speeds,
    uint8_t count)
{
    uint8_t i;
    bool keep;

    if (rt == NULL || speeds == NULL || count == 0u) {
        return;
    }
    memcpy(rt->turbo_speeds, speeds, sizeof(rt->turbo_speeds));
    rt->turbo_speed_count = count;
    memcpy(rt->config.turbo_speeds, speeds, sizeof(rt->config.turbo_speeds));
    rt->config.turbo_speed_count = count;

    keep = false;
    for (i = 0; i < count; i++) {
        if (rt->turbo_speeds[i] == rt->active_turbo_multiplier) {
            keep = true;
            break;
        }
    }
    if (!keep) {
        runtime_set_active_turbo(rt, rt->turbo_speeds[0]);
    }
    rt->config.active_turbo_multiplier = rt->active_turbo_multiplier;
}

static void runtime_set_active_turbo(runtime *rt, uint32_t milli_mhz)
{
    bool was_max;
    bool now_max;

    if (rt == NULL) {
        return;
    }
    was_max = runtime_turbo_is_max_value(rt->active_turbo_multiplier);
    now_max = runtime_turbo_is_max_value(milli_mhz);
    rt->active_turbo_multiplier = milli_mhz;
    rt->pace_initialized = false;
    runtime_apply_turbo_video_policy(rt, was_max && !now_max);
    if (now_max && !was_max) {
        runtime_history_apply_max_policy(rt, true, false);
    } else if (!now_max && was_max) {
        runtime_history_apply_max_policy(rt, false, true);
    }
    if (now_max && rt->machine_ready) {
        /* Immediate presentation frame so max is never blank. */
        apple2_video_paint_full_frame(&rt->machine);
        runtime_publish_canonical_frame(
            rt, RUNTIME_FRAME_PUBLISH_TRANSITION_CANONICAL, 0u);
    }
}

/* Max free-run: burn ~1/60 s wall with instruction quanta, then paint once. */
static void runtime_free_run_max_quantum(runtime *rt)
{
    uint64_t now;
    uint64_t frequency;
    uint64_t step;
    uint64_t deadline;
    uint32_t guard;

    if (rt == NULL) {
        return;
    }

    now = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    step = frequency / 60u;
    if (step == 0u) {
        step = 1u;
    }
    /* Always fill a wall quantum with emulation (a2m-shaped max). */
    deadline = now + step;
    rt->block_paint_initialized = true;
    rt->next_block_paint_counter = deadline;

    /*
     * Instruction-quantized free-run (S2). No per-Φ0 paint/audio; A-lite
     * H/V/VBL rides each insn so $C019 waiters still complete.
     * Guard caps a pathological host so the command queue still services.
     */
    {
        const bool any_exec_bp =
            (rt->breakpoint_count > 0 || rt->temp_bp_active);
        const bool type_active = rt->type_script_active;

        for (guard = 0u; guard < 2000000u; guard++) {
            size_t ran;

            if (!rt->suppress_execute_bp && any_exec_bp &&
                runtime_at_instruction_boundary(rt) &&
                runtime_breakpoint_matches_pc(rt)) {
                if (rt->temp_bp_active &&
                    rt->temp_bp_skip_current &&
                    rt->machine.cpu.cpu.pc == rt->temp_bp_address) {
                    rt->temp_bp_skip_current = false;
                } else {
                    runtime_pause_for_breakpoint(rt);
                    return;
                }
            }

            ran = apple2_step_instruction_max(&rt->machine);
            runtime_inspector_after_step(rt);
            if (ran == 0u) {
                rt->exec_state = RUNTIME_EXEC_PAUSED;
                rt->last_stop_reason = RUNTIME_STOP_REASON_ERROR;
                runtime_publish_error(rt, "step_instruction_max failed");
                runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
                return;
            }

            if (type_active) {
                runtime_type_script_tick(rt, (uint32_t)ran);
            }
            if (runtime_pause_if_breakpoint_pending(rt)) {
                return;
            }
            if (rt->suppress_execute_bp && runtime_at_instruction_boundary(rt)) {
                rt->suppress_execute_bp = false;
            }

            /* Timer check every 64 insns — less host call tax in the hot loop. */
            if ((guard & 63u) == 63u &&
                SDL_GetPerformanceCounter() >= deadline) {
                break;
            }
        }
    }

    /* Presentation paint ~60 Hz wall — not blank warp. */
    apple2_video_paint_full_frame(&rt->machine);
    runtime_publish_canonical_frame(
        rt, RUNTIME_FRAME_PUBLISH_MAX_CADENCE_CANONICAL, 0u);
}

/* Speaker soft-square amplitude (pre-AC-couple). Modest so MB can share headroom. */
static const float RUNTIME_SPEAKER_AMP = 0.22f;
/* Mockingboard channel gain after box-decimation (unipolar AY → host). */
static const float RUNTIME_MB_GAIN = 0.70f;
/* After AC-coupling, |x| below this → exact 0 (~-80 dBFS). */
static const float RUNTIME_AUDIO_QUIET = 1.0e-4f;
/* Sub-renders per host sample before LPF / downsample. */
enum { RUNTIME_AUDIO_OVERSAMPLE = 4 };

/* One-pole DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1].
   $C030 latches ±level; without AC-coupling idle is a DC shelf. Also centers
   unipolar AY levels. R ≈ 30 Hz high-pass, scaled with host rate. */
static float runtime_audio_dc_block(runtime *rt, int ch, float x)
{
    float r;
    float y;

    r = 1.0f - (2.0f * 3.14159265f * 30.0f) / (float)rt->audio_sample_rate;
    if (r < 0.990f) {
        r = 0.990f;
    } else if (r > 0.9995f) {
        r = 0.9995f;
    }

    y = x - rt->audio_dc_x_prev[ch] + r * rt->audio_dc_y_prev[ch];
    rt->audio_dc_x_prev[ch] = x;
    rt->audio_dc_y_prev[ch] = y;
    return y;
}

/* Cascaded one-pole LPF ≈ gentle 2nd-order anti-alias after oversample box. */
static float runtime_audio_lpf_alpha(int sample_rate)
{
    const float cutoff_hz = 14000.0f;
    float dt;
    float rc;

    if (sample_rate <= 0) {
        return 0.5f;
    }
    dt = 1.0f / (float)sample_rate;
    rc = 1.0f / (2.0f * 3.14159265f * cutoff_hz);
    return dt / (rc + dt);
}

static float runtime_audio_lpf2(runtime *rt, int ch, float x, float alpha)
{
    float y1 = alpha * x + (1.0f - alpha) * rt->audio_mb_lpf1[ch];
    float y2;

    rt->audio_mb_lpf1[ch] = y1;
    y2 = alpha * y1 + (1.0f - alpha) * rt->audio_mb_lpf2[ch];
    rt->audio_mb_lpf2[ch] = y2;
    return y2;
}

static float runtime_audio_clamp(float x)
{
    if (x > 1.0f) {
        return 1.0f;
    }
    if (x < -1.0f) {
        return -1.0f;
    }
    return x;
}

static float runtime_audio_quiet_gate(runtime *rt, int ch, float sample)
{
    if (sample > -RUNTIME_AUDIO_QUIET && sample < RUNTIME_AUDIO_QUIET) {
        sample = 0.0f;
        if (rt->audio_dc_y_prev[ch] > -RUNTIME_AUDIO_QUIET &&
            rt->audio_dc_y_prev[ch] < RUNTIME_AUDIO_QUIET) {
            rt->audio_dc_y_prev[ch] = 0.0f;
        }
    }
    return sample;
}

static MOCKINGBOARD *runtime_primary_mockingboard(runtime *rt)
{
    if (rt->machine.mb_slot > 0 && rt->machine.mb_slot <= 7) {
        return &rt->machine.mockingboard[rt->machine.mb_slot];
    }
    return NULL;
}

/* Emit host audio for `cpu_cycles` of machine time.
   - 1×: stereo PCM into the ring (speaker center + MB L/R).
   - Free-run / warp: advance AY chip time only (no host PCM) so pending does
     not pile up and music stays aligned with the CPU when 1× resumes.
   Host buffer layout is interleaved float L,R pairs. */
static void runtime_produce_audio(runtime *rt, uint32_t cpu_cycles)
{
    MOCKINGBOARD *mb;
    double cycles_per_sample;
    float samples[256 * 2];
    size_t produced = 0;
    float lpf_alpha;

    if (rt == NULL || cpu_cycles == 0u) {
        return;
    }
    if (rt->inspecting) {
        return;
    }

    mb = runtime_primary_mockingboard(rt);

    if (runtime_turbo_is_free_run(rt)) {
        if (mb != NULL) {
            mockingboard_reconcile_audio_state(mb);
        }
        rt->audio_cycle_accum = 0.0;
        return;
    }

    if (rt->audio_out == NULL || rt->audio_sample_rate <= 0) {
        /* Headless / no device: still drain AY time so queues stay honest. */
        if (mb != NULL) {
            mockingboard_reconcile_audio_state(mb);
        }
        return;
    }

    cycles_per_sample = APPLE2_CPU_FREQUENCY_HZ / (double)rt->audio_sample_rate;
    if (cycles_per_sample <= 0.0) {
        return;
    }

    lpf_alpha = runtime_audio_lpf_alpha(rt->audio_sample_rate);

    rt->audio_cycle_accum += (double)cpu_cycles;
    while (rt->audio_cycle_accum >= cycles_per_sample && produced < 256u) {
        float speaker;
        float left;
        float right;
        uint32_t step = (uint32_t)cycles_per_sample;
        uint32_t base;
        uint32_t rem;
        uint32_t sub_count = 0;
        float sum_l = 0.0f;
        float sum_r = 0.0f;
        unsigned i;

        if (step < 1u) {
            step = 1u;
        }

        /* Latched bipolar $C030 level; AC-coupled below → idle is silence. */
        speaker = rt->machine.speaker_level ? RUNTIME_SPEAKER_AMP : -RUNTIME_SPEAKER_AMP;

        if (mb != NULL) {
            /* 4× box oversample: advance chip over the host interval in chunks,
               average, then LPF. Cuts square/noise aliases into 48 kHz. */
            base = step / (uint32_t)RUNTIME_AUDIO_OVERSAMPLE;
            rem = step % (uint32_t)RUNTIME_AUDIO_OVERSAMPLE;
            for (i = 0; i < (unsigned)RUNTIME_AUDIO_OVERSAMPLE; i++) {
                uint32_t sub = base + (i < rem ? 1u : 0u);
                MOCKINGBOARD_SAMPLE s;

                if (sub == 0u) {
                    continue;
                }
                s = mockingboard_render_audio_sample(mb, sub);
                sum_l += s.left;
                sum_r += s.right;
                sub_count++;
            }
            if (sub_count > 0u) {
                sum_l = (sum_l / (float)sub_count) * RUNTIME_MB_GAIN;
                sum_r = (sum_r / (float)sub_count) * RUNTIME_MB_GAIN;
            }
        }

        /* Post-decimation tone smooth / anti-alias (even when MB silent). */
        left = runtime_audio_lpf2(rt, 0, sum_l, lpf_alpha);
        right = runtime_audio_lpf2(rt, 1, sum_r, lpf_alpha);

        left += speaker;
        right += speaker;

        left = runtime_audio_clamp(runtime_audio_dc_block(rt, 0, left));
        right = runtime_audio_clamp(runtime_audio_dc_block(rt, 1, right));
        left = runtime_audio_quiet_gate(rt, 0, left);
        right = runtime_audio_quiet_gate(rt, 1, right);

        samples[produced * 2u] = left;
        samples[produced * 2u + 1u] = right;
        produced++;
        rt->audio_cycle_accum -= cycles_per_sample;
    }
    if (produced > 0u) {
        (void)audio_buffer_write(rt->audio_out, samples, produced * 2u);
    }
}

static void runtime_reset_pacer(runtime *rt)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    double target_hz = runtime_turbo_target_hz(rt->active_turbo_multiplier);
    double mhz;

    /* Finite N MHz: wall time per video frame scales as 1/N (N× emulated rate). */
    if (target_hz <= 0.0) {
        mhz = 1.0;
    } else {
        mhz = target_hz / APPLE2_CPU_FREQUENCY_HZ;
        if (mhz < 0.001) {
            mhz = 0.001;
        }
    }
    rt->frame_counter_step =
        (uint64_t)((double)frequency * (double)APPLE2_VIDEO_CYCLES_PER_FRAME /
                   (APPLE2_CPU_FREQUENCY_HZ * mhz));
    if (rt->frame_counter_step == 0) {
        rt->frame_counter_step = 1;
    }
    rt->next_frame_counter = SDL_GetPerformanceCounter() + rt->frame_counter_step;
    rt->pace_initialized = true;
}

static void runtime_pace_after_frame(runtime *rt)
{
    uint64_t now;
    uint64_t frequency;

    if (runtime_turbo_is_free_run(rt) || (rt != NULL && rt->inspecting)) {
        return;
    }
    if (!rt->pace_initialized) {
        runtime_reset_pacer(rt);
        return;
    }
    now = SDL_GetPerformanceCounter();
    frequency = SDL_GetPerformanceFrequency();
    if (now < rt->next_frame_counter) {
        uint64_t remaining = rt->next_frame_counter - now;
        uint32_t delay_ms = (uint32_t)((remaining * 1000u) / frequency);
        if (delay_ms > 0) {
            SDL_Delay(delay_ms);
        }
        while (SDL_GetPerformanceCounter() < rt->next_frame_counter) {
            SDL_Delay(0);
        }
    }
    rt->next_frame_counter += rt->frame_counter_step;
    now = SDL_GetPerformanceCounter();
    if (rt->next_frame_counter < now) {
        rt->next_frame_counter = now + rt->frame_counter_step;
    }
}

static void runtime_publish_argb_pixels(
    runtime *rt,
    const uint32_t *pixels,
    runtime_frame_publish_kind kind,
    uint64_t inspector_picture_id)
{
    uint32_t w = APPLE2_VIDEO_WIDTH;
    uint32_t h = APPLE2_VIDEO_HEIGHT;
    size_t nbytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    runtime_event event;
    uint64_t frame_number;
    uint64_t machine_cycle;

    if (rt == NULL || pixels == NULL) {
        return;
    }

    frame_number = rt->machine.video.frame_number;
    machine_cycle = apple2_cycles(&rt->machine);

    mutex_lock(rt->frame_slot.mutex);
    if (rt->frame_slot.argb == NULL) {
        rt->frame_slot.argb = (uint32_t *)malloc(nbytes);
    }
    if (rt->frame_slot.argb != NULL) {
        if (rt->frame_slot.has_frame) {
            rt->frame_slot.dropped_frames++;
        }
        memcpy(rt->frame_slot.argb, pixels, nbytes);
        rt->frame_slot.width = w;
        rt->frame_slot.height = h;
        rt->frame_slot.frame_number = frame_number;
        rt->frame_slot.has_frame = true;
        rt->frame_slot.published_frames++;
    }
    mutex_unlock(rt->frame_slot.mutex);

    /* Rolling screen log (C2). Forensic publishes the live slot only — the
       ring is a recorder and must not grow while standing on the tape. */
    if (!rt->inspecting &&
        (kind == RUNTIME_FRAME_PUBLISH_FINITE_CADENCE_CANONICAL ||
         kind == RUNTIME_FRAME_PUBLISH_MAX_CADENCE_CANONICAL)) {
        (void)runtime_frame_ring_push(
            &rt->frame_ring,
            inspector_picture_id,
            frame_number,
            machine_cycle,
            w,
            h,
            pixels);
    }

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_FRAME_READY;
    event.data.frame_ready.frame_number = frame_number;
    event.data.frame_ready.machine_cycle = machine_cycle;
    event.data.frame_ready.dropped_frames = rt->frame_slot.dropped_frames;
    {
        uint8_t mask = 0;
        int s;
        for (s = 1; s <= 7; s++) {
            if (rt->machine.diskii_present[s] &&
                (rt->machine.diskii_controller[s].diskii_drive[0].motor_on ||
                 rt->machine.diskii_controller[s].diskii_drive[1].motor_on)) {
                mask = (uint8_t)(mask | (1u << s));
            }
        }
        event.data.frame_ready.disk_motor_mask = mask;
    }
    runtime_publish_event(rt, &event);
}

static void runtime_publish_canonical_frame(
    runtime *rt,
    runtime_frame_publish_kind kind,
    uint64_t inspector_picture_id)
{
    const uint32_t *fb;

    if (rt == NULL) {
        return;
    }
    fb = apple2_video_framebuffer(&rt->machine);
    runtime_publish_argb_pixels(rt, fb, kind, inspector_picture_id);
}

static bool runtime_paint_presentation_scratch(runtime *rt)
{
    const size_t pixels =
        (size_t)APPLE2_VIDEO_WIDTH * (size_t)APPLE2_VIDEO_HEIGHT;

    if (rt == NULL) {
        return false;
    }
    if (rt->presentation_scratch == NULL) {
        rt->presentation_scratch =
            (uint32_t *)malloc(pixels * sizeof(*rt->presentation_scratch));
    }
    return rt->presentation_scratch != NULL &&
        apple2_video_paint_full_frame_to(
            &rt->machine, rt->presentation_scratch, pixels);
}

/*
 * Present the CRT after a stop (or on REQUEST_FRAME).
 * Override, max turbo, and paint-off (sealed run) have no trustworthy beam
 * image — dump video RAM. Otherwise publish the beam buffer so a mid-frame
 * mode switch stays visible.
 */
static void runtime_publish_presented_frame(runtime *rt)
{
    if (rt == NULL || !rt->machine_ready || rt->machine.video.fb == NULL) {
        return;
    }
    if (rt->machine.video.display_override_enabled ||
        !rt->machine.video.paint_enabled ||
        runtime_turbo_is_free_run(rt)) {
        if (runtime_paint_presentation_scratch(rt)) {
            runtime_publish_argb_pixels(
                rt,
                rt->presentation_scratch,
                RUNTIME_FRAME_PUBLISH_HOST_ONLY,
                0u);
            return;
        }
    }
    runtime_publish_canonical_frame(rt, RUNTIME_FRAME_PUBLISH_HOST_ONLY, 0u);
}

static void runtime_maybe_frame(runtime *rt)
{
    if (!apple2_video_take_frame_ready(&rt->machine)) {
        return;
    }
    if (runtime_turbo_is_free_run(rt)) {
        /* Max: live path is wall-paced block paint, not beam frame_ready. */
        return;
    }
    runtime_publish_canonical_frame(
        rt, RUNTIME_FRAME_PUBLISH_FINITE_CADENCE_CANONICAL, 0u);
    runtime_pace_after_frame(rt);
}

static uint8_t runtime_read_byte(runtime *rt, uint16_t addr, runtime_memory_mode mode)
{
    return apple2_read_in_view(&rt->machine, runtime_mode_to_view_flags(mode), addr);
}

static void runtime_write_byte(runtime *rt, uint16_t addr, uint8_t value, runtime_memory_mode mode)
{
    apple2_write_in_view(&rt->machine, runtime_mode_to_view_flags(mode), addr, value);
}

/* Paused memory edits always dump video RAM so the CRT tracks typed bytes.
 * This replaces any mid-frame beam image (accepted debugger tradeoff). */
static void runtime_refresh_display_after_memory_edit(runtime *rt)
{
    if (rt == NULL || !rt->machine_ready || rt->machine.video.fb == NULL) {
        return;
    }
    if (runtime_paint_presentation_scratch(rt)) {
        runtime_publish_argb_pixels(
            rt,
            rt->presentation_scratch,
            RUNTIME_FRAME_PUBLISH_HOST_ONLY,
            0u);
    }
}

static void runtime_handle_request_memory(runtime *rt, const runtime_command *cmd)
{
    runtime_memory_mode mode = (runtime_memory_mode)cmd->data.request_memory.mode;
    uint16_t address = cmd->data.request_memory.address;
    uint32_t length = cmd->data.request_memory.length;
    uint64_t token = cmd->request_token;
    uint32_t i;

    if (length == 0u || length > RUNTIME_MEMORY_RPC_MAX_LENGTH) {
        return;
    }

    if (token == 0u) {
        runtime_event event;
        uint32_t copy_len = length;
        if (copy_len > RUNTIME_MEMORY_SNAPSHOT_MAX) {
            copy_len = RUNTIME_MEMORY_SNAPSHOT_MAX;
        }
        memset(&event, 0, sizeof(event));
        event.type = RUNTIME_EVENT_MEMORY_RESPONSE;
        event.data.memory.address = address;
        event.data.memory.length = (uint16_t)copy_len;
        event.data.memory.mode = mode;
        for (i = 0; i < copy_len; i++) {
            event.data.memory.bytes[i] =
                runtime_read_byte(rt, (uint16_t)(address + i), mode);
        }
        runtime_publish_event(rt, &event);
        return;
    }

    {
        uint8_t *bytes = (uint8_t *)malloc(length);
        runtime_event event;
        bool parked = false;
        size_t j;

        memset(&event, 0, sizeof(event));
        event.type = RUNTIME_EVENT_MEMORY_RPC_COMPLETE;
        event.request_token = token;
        event.data.memory_rpc.address = address;
        event.data.memory_rpc.length = length;
        event.data.memory_rpc.mode = mode;

        if (bytes == NULL) {
            event.data.memory_rpc.status = RUNTIME_MEMORY_RPC_ERROR;
            runtime_publish_event(rt, &event);
            return;
        }
        for (i = 0; i < length; i++) {
            bytes[i] = runtime_read_byte(rt, (uint16_t)(address + i), mode);
        }
        mutex_lock(rt->rpc_payload_pool.mutex);
        for (j = 0; j < RUNTIME_RPC_PAYLOAD_POOL_CAPACITY; j++) {
            runtime_rpc_payload_slot *slot = &rt->rpc_payload_pool.slots[j];
            if (!slot->in_use) {
                slot->in_use = 1;
                slot->kind = RUNTIME_RPC_PAYLOAD_MEMORY;
                slot->request_token = token;
                slot->meta.memory.address = address;
                slot->meta.memory.mode = mode;
                slot->length = length;
                slot->bytes = bytes;
                parked = true;
                break;
            }
        }
        mutex_unlock(rt->rpc_payload_pool.mutex);
        if (!parked) {
            free(bytes);
            event.data.memory_rpc.status = RUNTIME_MEMORY_RPC_BUSY;
        } else {
            event.data.memory_rpc.status = RUNTIME_MEMORY_RPC_OK;
        }
        runtime_publish_event(rt, &event);
    }
}

static void runtime_fill_debug_memory(runtime *rt, bool include_write_history)
{
    uint32_t a;
    runtime_debug_memory_snapshot *snap;

    mutex_lock(rt->debug_memory_slot.mutex);
    snap = &rt->debug_memory_slot.snapshot;
    memset(snap, 0, sizeof(*snap));
    snap->generation = ++rt->debug_memory_slot.generation;
    snap->has_write_history = include_write_history ? 1u : 0u;
    for (a = 0; a < 65536u; a++) {
        uint16_t addr = (uint16_t)a;
        snap->map[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_MAP), addr);
        snap->ram[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_MAIN), addr);
        snap->rom[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_ROM), addr);
        snap->aux[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_AUX), addr);
        snap->lc1[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_LC1), addr);
        snap->lc2[a] = apple2_read_in_view(
            &rt->machine, view_flags_from_area(RUNTIME_VIEW_AREA_LC2), addr);
        snap->aux_valid[a] = 1;
        snap->lc1_valid[a] = 1;
        snap->lc2_valid[a] = 1;
        if (include_write_history) {
            snap->write_history[a] =
                apple2_debug_read_write_history(&rt->machine, addr);
        }
    }
    rt->debug_memory_slot.has_snapshot = true;
    mutex_unlock(rt->debug_memory_slot.mutex);
    runtime_publish_simple(rt, RUNTIME_EVENT_DEBUG_MEMORY_READY);
}

enum { RUNTIME_STEP_NESTED_FAST_LIMIT = 100000 };

static bool runtime_inspector_pause_at_live(runtime *rt)
{
    if (rt == NULL || !rt->inspecting || !runtime_inspector_at_live(rt)) {
        return false;
    }
    (void)runtime_inspector_restore_live(rt);
    rt->temp_bp_active = false;
    runtime_publish_presented_frame(rt);
    if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_RUN_COMPLETE;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUN_COMPLETE);
        runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
        runtime_inspector_publish_head(rt);
    }
    return true;
}

static bool runtime_exec_step_instruction(runtime *rt)
{
    uint64_t c0;

    if (rt == NULL) {
        return false;
    }
    c0 = apple2_cycles(&rt->machine);
    if (!apple2_step_instruction(&rt->machine)) {
        return false;
    }
    if (rt->inspecting) {
        runtime_inspector_apply_logged_inputs(
            rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
        if (runtime_inspector_at_live(rt)) {
            (void)runtime_inspector_restore_live(rt);
        } else {
            runtime_inspector_sync_focus(rt);
        }
    } else {
        runtime_inspector_after_step(rt);
    }
    return true;
}

static void runtime_step_over(runtime *rt, bool *alive)
{
    uint8_t opcode;
    uint16_t stop_pc;
    int jsr_counter = 0;
    int fast_limit = 0;

    (void)alive;
    if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
        return;
    }
    if (rt->inspecting && runtime_inspector_at_live(rt)) {
        return;
    }
    runtime_finish_to_instruction_boundary(rt);
    opcode = apple2_debug_read(&rt->machine, rt->machine.cpu.cpu.pc);
    if (opcode != 0x20u) {
        (void)runtime_exec_step_instruction(rt);
        runtime_maybe_frame(rt);
        rt->suppress_execute_bp = false;
        if (!runtime_inspector_pause_at_live(rt)) {
            runtime_pause_for_step(rt);
        }
        return;
    }
    stop_pc = (uint16_t)(rt->machine.cpu.cpu.pc + 3u);
    rt->suppress_execute_bp = true;
    rt->exec_state = RUNTIME_EXEC_RUNNING;
    runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    for (;;) {
        if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return;
        }
        opcode = apple2_debug_read(&rt->machine, rt->machine.cpu.cpu.pc);
        (void)runtime_exec_step_instruction(rt);
        runtime_maybe_frame(rt);
        if (runtime_inspector_pause_at_live(rt)) {
            return;
        }
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        rt->suppress_execute_bp = false;
        if (opcode == 0x20u) {
            jsr_counter++;
        } else if (opcode == 0x60u) {
            jsr_counter--;
        }
        if (jsr_counter <= 0 && rt->machine.cpu.cpu.pc == stop_pc) {
            runtime_pause_for_step(rt);
            return;
        }
        if (++fast_limit >= RUNTIME_STEP_NESTED_FAST_LIMIT) {
            return;
        }
    }
}

static void runtime_step_out(runtime *rt, bool *alive)
{
    uint8_t opcode;
    int jsr_counter = 1;
    int fast_limit = 0;

    (void)alive;
    if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
        return;
    }
    if (rt->inspecting && runtime_inspector_at_live(rt)) {
        return;
    }
    runtime_finish_to_instruction_boundary(rt);
    rt->suppress_execute_bp = true;
    rt->exec_state = RUNTIME_EXEC_RUNNING;
    runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    for (;;) {
        if (!rt->suppress_execute_bp && runtime_breakpoint_matches_pc(rt)) {
            runtime_pause_for_breakpoint(rt);
            return;
        }
        opcode = apple2_debug_read(&rt->machine, rt->machine.cpu.cpu.pc);
        (void)runtime_exec_step_instruction(rt);
        runtime_maybe_frame(rt);
        if (runtime_inspector_pause_at_live(rt)) {
            return;
        }
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        rt->suppress_execute_bp = false;
        if (opcode == 0x20u) {
            jsr_counter++;
        } else if (opcode == 0x60u) {
            jsr_counter--;
            if (jsr_counter <= 0) {
                runtime_pause_for_step(rt);
                return;
            }
        }
        if (++fast_limit >= RUNTIME_STEP_NESTED_FAST_LIMIT) {
            return;
        }
    }
}

static void runtime_run_to_cursor(runtime *rt, uint16_t address, bool *alive)
{
    int fast_limit = 0;
    (void)alive;
    if (rt->inspecting && runtime_inspector_at_live(rt)) {
        return;
    }
    runtime_finish_to_instruction_boundary(rt);
    rt->temp_bp_active = true;
    rt->temp_bp_address = address;
    rt->temp_bp_skip_current = (rt->machine.cpu.cpu.pc == address);
    rt->suppress_execute_bp = false;
    rt->exec_state = RUNTIME_EXEC_RUNNING;
    rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
    runtime_reset_pacer(rt);
    runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    for (;;) {
        if (runtime_at_instruction_boundary(rt) &&
            rt->temp_bp_active &&
            rt->machine.cpu.cpu.pc == address) {
            if (rt->temp_bp_skip_current) {
                rt->temp_bp_skip_current = false;
            } else {
                rt->temp_bp_active = false;
                runtime_pause_for_breakpoint(rt);
                return;
            }
        }
        (void)runtime_exec_step_instruction(rt);
        runtime_maybe_frame(rt);
        if (runtime_inspector_pause_at_live(rt)) {
            return;
        }
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        if (++fast_limit >= RUNTIME_STEP_NESTED_FAST_LIMIT) {
            return;
        }
    }
}

/* Debugger poke of A/X/Y also refreshes N/Z as if that value had just been
   loaded (LDA/LDX/LDY). Branches and other flag users then match the number
   the user typed without requiring them to edit status bits by hand. C/V/I/D
   are left alone. */
static void runtime_set_register_nz_from_value(CPU *cpu, uint8_t value)
{
    if (cpu == NULL) {
        return;
    }
    cpu->N = (value & 0x80u) != 0u ? 1u : 0u;
    cpu->Z = value == 0u ? 1u : 0u;
}

static void runtime_set_register(runtime *rt, runtime_cpu_register reg, uint16_t value)
{
    CPU *cpu = &rt->machine.cpu.cpu;
    if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
        runtime_publish_cpu(rt, 0u);
        return;
    }
    runtime_finish_to_instruction_boundary(rt);
    switch (reg) {
    case RUNTIME_CPU_REGISTER_PC:
        cpu->pc = value;
        break;
    case RUNTIME_CPU_REGISTER_SP:
        cpu->sp = (uint16_t)(0x0100u | (value & 0xFFu));
        break;
    case RUNTIME_CPU_REGISTER_A:
        cpu->A = (uint8_t)value;
        runtime_set_register_nz_from_value(cpu, cpu->A);
        break;
    case RUNTIME_CPU_REGISTER_X:
        cpu->X = (uint8_t)value;
        runtime_set_register_nz_from_value(cpu, cpu->X);
        break;
    case RUNTIME_CPU_REGISTER_Y:
        cpu->Y = (uint8_t)value;
        runtime_set_register_nz_from_value(cpu, cpu->Y);
        break;
    case RUNTIME_CPU_REGISTER_STATUS:
        cpu->flags = (uint8_t)value;
        break;
    default:
        break;
    }
    runtime_publish_cpu(rt, 0u);
}

/* Host typing → Apple $C000 strobe. Shift/Ctrl tracked on press/release.
   Open/Closed Apple are //e solid-apple keys (also BUTN0/BUTN1). */
static void runtime_inject_host_key(runtime *rt, host_key key, bool pressed)
{
    uint8_t strobe;

    if (key == HOST_KEY_SHIFT || key == HOST_KEY_CTRL) {
        host_keyboard_set_key(&rt->host_keyboard, key, pressed);
        return;
    }
    if (key == HOST_KEY_OPEN_APPLE) {
        if (pressed) {
            rt->machine.state_flags |= A2S_OPEN_APPLE;
        } else {
            rt->machine.state_flags &= ~A2S_OPEN_APPLE;
        }
        return;
    }
    if (key == HOST_KEY_CLOSED_APPLE) {
        if (pressed) {
            rt->machine.state_flags |= A2S_CLOSED_APPLE;
        } else {
            rt->machine.state_flags &= ~A2S_CLOSED_APPLE;
        }
        return;
    }
    if (!pressed) {
        /* Key-up clears KEY_HELD so the next press can re-strobe. */
        rt->machine.state_flags &= ~A2S_KEY_HELD;
        return;
    }
    strobe = host_key_to_apple_strobe(
        key,
        rt->host_keyboard.shift_held,
        rt->host_keyboard.ctrl_held);
    if (strobe != 0) {
        apple2_set_key(&rt->machine, strobe);
    }
}

static void runtime_publish_symbols(runtime *rt)
{
    runtime_symbol_slot *slot = &rt->symbol_slot;
    runtime_symbol_snapshot *snap = &slot->snapshot;
    symbol_info info;
    size_t total;
    size_t i;

    mutex_lock(slot->mutex);
    total = symbol_table_count(rt->symbols);
    snap->total = total;
    snap->count = total < RUNTIME_SYMBOL_SNAPSHOT_MAX ?
        total : RUNTIME_SYMBOL_SNAPSHOT_MAX;
    for (i = 0; i < snap->count; ++i) {
        if (symbol_table_get(rt->symbols, i, &info) == SYMBOL_OK) {
            snap->entries[i].address = info.address;
            snprintf(
                snap->entries[i].name,
                RUNTIME_SYMBOL_NAME_MAX,
                "%s",
                info.name);
        } else {
            snap->entries[i].address = 0;
            snap->entries[i].name[0] = '\0';
        }
    }
    slot->has_symbols = true;
    mutex_unlock(slot->mutex);
}

static void runtime_publish_assemble_error(runtime *rt, const char *message)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_ASSEMBLE_ERROR;
    snprintf(
        event.data.error.message,
        sizeof(event.data.error.message),
        "%s",
        message != NULL ? message : "assembly failed");
    runtime_publish_event(rt, &event);
}

static void runtime_publish_assemble_complete(
    runtime *rt,
    const char *path,
    uint16_t address,
    const char *notice)
{
    runtime_event event;
    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_ASSEMBLE_COMPLETE;
    event.data.assemble.address = address;
    snprintf(
        event.data.assemble.path,
        sizeof(event.data.assemble.path),
        "%s",
        path != NULL ? path : "");
    snprintf(
        event.data.assemble.notice,
        sizeof(event.data.assemble.notice),
        "%s",
        notice != NULL ? notice : "");
    runtime_publish_event(rt, &event);
}

static void runtime_assemble_file_command(
    runtime *rt,
    const runtime_command *command)
{
    char error[4096];
    char notice[4096];
    uint16_t assembled_start = command->data.assemble_file.address;
    uint16_t assembled_end = assembled_start;
    uint32_t assembled_count = 0u;
    runtime_assembler_options options;
    const bool mli_launch = command->data.assemble_file.mli_launch != 0u;
    /* MLI launch wins over a stale reset_first bit from the command. */
    const bool do_reset =
        command->data.assemble_file.reset_first != 0u && !mli_launch;

    memset(&options, 0, sizeof(options));
    options.auto_adjust_segments =
        command->data.assemble_file.auto_adjust_segments != 0u;
    options.enable_65c02 = rt->machine.model == APPLE2_MODEL_IIE_ENHANCED;

    if (mli_launch && rt->exec_state == RUNTIME_EXEC_RUNNING) {
        runtime_finish_to_instruction_boundary(rt);
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_PAUSE_COMMAND;
        runtime_publish_machine(rt);
        runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
        runtime_publish_cpu(rt, 0u);
    }

    runtime_history_prepare_discontinuity(rt);
    if (do_reset) {
        apple2_reset(&rt->machine);
        if (rt->history != NULL) {
            runtime_history_status status;
            (void)runtime_history_transition_timeline(rt->history);
            runtime_history_get_status(rt->history, &status);
            if (status.available && status.recording) {
                (void)runtime_history_append_marker(
                    rt->history,
                    RUNTIME_HISTORY_MARKER_RESET_COMPLETE,
                    RUNTIME_HISTORY_RESET_ASSEMBLE_RESET_FIRST,
                    0u,
                    apple2_cycles(&rt->machine));
            }
        }
    }

    if (!runtime_assemble_file_ex_options(
            &rt->machine,
            rt->symbols,
            command->data.assemble_file.path,
            command->data.assemble_file.address,
            command->data.assemble_file.path,
            &options,
            &assembled_start,
            &assembled_end,
            &assembled_count,
            notice,
            sizeof(notice),
            error,
            sizeof(error))) {
        runtime_publish_assemble_error(
            rt, error[0] != '\0' ? error : "assembly failed");
        return;
    }

    if (rt->history != NULL) {
        (void)runtime_history_append_marker(
            rt->history,
            RUNTIME_HISTORY_MARKER_ASSEMBLE,
            assembled_start,
            assembled_count,
            apple2_cycles(&rt->machine));
    }
    runtime_publish_symbols(rt);
    if (command->data.assemble_file.auto_run != 0u) {
        bool allow_auto_run = true;
        if (mli_launch &&
            apple2_debug_read(&rt->machine, 0xBF00u) != 0x4Cu) {
            allow_auto_run = false;
            snprintf(
                notice,
                sizeof(notice),
                "MLI launch skipped: ProDOS MLI not present at $BF00");
        }
        if (allow_auto_run) {
            rt->machine.cpu.cpu.pc = command->data.assemble_file.run_address;
            rt->machine.cpu.cpu.sp = 0x01FFu;
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
            runtime_reset_pacer(rt);
            runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        }
    }
    runtime_publish_assemble_complete(
        rt, command->data.assemble_file.path, assembled_start, notice);
    runtime_publish_cpu(rt, 0u);
    runtime_publish_machine(rt);
}

static bool runtime_inspector_command_mutates_machine(runtime_command_type type)
{
    switch (type) {
    case RUNTIME_COMMAND_RESET:
    case RUNTIME_COMMAND_WRITE_MEMORY_BYTE:
    case RUNTIME_COMMAND_WRITE_MEMORY:
    case RUNTIME_COMMAND_SET_CPU_REGISTER:
    case RUNTIME_COMMAND_LOAD_STATE:
    case RUNTIME_COMMAND_SAVE_STATE:
    case RUNTIME_COMMAND_LOAD_BIN:
    case RUNTIME_COMMAND_ASSEMBLE_FILE:
    case RUNTIME_COMMAND_MEDIA_INSERT:
    case RUNTIME_COMMAND_MEDIA_EJECT:
    case RUNTIME_COMMAND_MEDIA_SWAP:
    case RUNTIME_COMMAND_BOOT_SLOT:
    case RUNTIME_COMMAND_APPLY_MACHINE_CONFIG:
    case RUNTIME_COMMAND_PASTE_TEXT:
    case RUNTIME_COMMAND_KEYBOARD_KEY:
    case RUNTIME_COMMAND_SET_GAMEPORT:
    case RUNTIME_COMMAND_HISTORY_CLEAR:
    case RUNTIME_COMMAND_HISTORY_RECORD:
    case RUNTIME_COMMAND_INSPECTOR_SET_ENABLED:
        return true;
    default:
        return false;
    }
}

static void runtime_inspector_reattach_live_hooks(runtime *rt)
{
    apple2_set_memory_access_callback(&rt->machine, runtime_on_memory_access, rt);
    runtime_history_sync_observer(rt);
}

static void runtime_publish_inspector_mode(
    runtime *rt,
    uint64_t token,
    uint8_t op,
    runtime_inspector_enter_status status)
{
    runtime_event event;
    runtime_inspector_window window;

    memset(&event, 0, sizeof(event));
    event.type = RUNTIME_EVENT_INSPECTOR_MODE;
    event.request_token = token;
    event.data.inspector_mode.op = op;
    event.data.inspector_mode.mode = (uint8_t)runtime_inspector_current_mode(rt);
    event.data.inspector_mode.status = (uint8_t)status;
    event.data.inspector_mode.focus = rt->inspector_focus;
    runtime_inspector_window_info(rt, &window);
    event.data.inspector_mode.start_kind = (uint8_t)window.start_kind;
    event.data.inspector_mode.start_arg1 = window.start_arg1;
    runtime_publish_event(rt, &event);
}

static void runtime_inspector_publish_head(runtime *rt)
{
    runtime_publish_cpu(rt, 0u);
    runtime_publish_machine(rt);
    if (rt->machine.video.fb != NULL) {
        runtime_publish_canonical_frame(
            rt, RUNTIME_FRAME_PUBLISH_HOST_ONLY, 0u);
    }
}

static void runtime_process_command(runtime *rt, const runtime_command *cmd, bool *alive)
{
    if (rt->inspecting && runtime_inspector_command_mutates_machine(cmd->type)) {
        runtime_publish_error_code(
            rt,
            RUNTIME_ERROR_READ_ONLY_INSPECTOR,
            "machine is read-only while Inspecting");
        return;
    }
    if (runtime_history_command_invalidates_cursor(cmd->type)) {
        runtime_history_invalidate_cursor(rt);
        runtime_publish_state_changed(
            rt,
            runtime_state_changed_reason_for_command(cmd->type),
            cmd->session_id);
    }
    switch (cmd->type) {
    case RUNTIME_COMMAND_PING: {
        runtime_event event;
        memset(&event, 0, sizeof(event));
        event.type = RUNTIME_EVENT_PONG;
        event.request_token = cmd->request_token;
        runtime_publish_event(rt, &event);
        break;
    }
    case RUNTIME_COMMAND_QUIT:
        *alive = false;
        break;
    case RUNTIME_COMMAND_RESET: {
        /* Match c64m: PRESERVE_STATE keeps prior run/pause; else honor flag. */
        bool was_running =
            cmd->data.reset.resume_running != RUNTIME_RESET_PRESERVE_STATE ?
            cmd->data.reset.resume_running == RUNTIME_RESET_RUNNING :
            rt->exec_state == RUNTIME_EXEC_RUNNING;

        runtime_history_prepare_discontinuity(rt);
        if (cmd->data.reset.cold != 0u) {
            apple2_cold_reset(&rt->machine);
        } else {
            apple2_reset(&rt->machine);
        }
        if (rt->history != NULL) {
            runtime_history_status st;
            (void)runtime_history_transition_timeline(rt->history);
            runtime_history_get_status(rt->history, &st);
            if (st.available && st.recording) {
                (void)runtime_history_append_marker(
                    rt->history,
                    RUNTIME_HISTORY_MARKER_RESET_COMPLETE,
                    RUNTIME_HISTORY_RESET_EXPLICIT,
                    0u,
                    apple2_cycles(&rt->machine));
            }
        }
        runtime_inspector_on_history_invalidate(rt);
        rt->suppress_execute_bp = false;
        rt->temp_bp_active = false;
        rt->breakpoint_hit_pending = false;
        runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
        runtime_publish_cpu(rt, 0u);
        runtime_publish_machine(rt);

        if (was_running) {
            rt->exec_state = RUNTIME_EXEC_RUNNING;
            rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
            runtime_reset_pacer(rt);
            runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        } else {
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            rt->last_stop_reason = RUNTIME_STOP_REASON_RESET;
            runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
        }
        break;
    }
    case RUNTIME_COMMAND_APPLY_MACHINE_CONFIG: {
        const runtime_machine_config *config = &cmd->data.apply_machine_config.config;
        bool changed = rt->machine.model !=
            (config->apple_model == 1u ? APPLE2_MODEL_II_PLUS : APPLE2_MODEL_IIE_ENHANCED);
        int mockingboards = 0;
        int slot;

        for (slot = 1; slot <= 7; ++slot) {
            runtime_slot_card_type type = config->slot_cards[slot];
            apple2_slot_type machine_type = rt->machine.slot_type[slot];
            runtime_slot_card_type current = RUNTIME_SLOT_CARD_EMPTY;
            if (type < RUNTIME_SLOT_CARD_EMPTY || type > RUNTIME_SLOT_CARD_MOCKINGBOARD) {
                runtime_publish_error(rt, "invalid slot card configuration");
                break;
            }
            if (type == RUNTIME_SLOT_CARD_MOCKINGBOARD) {
                ++mockingboards;
            }
            if (machine_type == SLOT_TYPE_DISKII) current = RUNTIME_SLOT_CARD_DISKII;
            else if (machine_type == SLOT_TYPE_SMARTPORT) current = RUNTIME_SLOT_CARD_SMARTPORT;
            else if (machine_type == SLOT_TYPE_MOCKINGBOARD) current = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            if (current != type) {
                changed = true;
            }
        }
        if (slot <= 7) {
            break;
        }
        if (config->slot_cards[0] != RUNTIME_SLOT_CARD_EMPTY || mockingboards > 1) {
            runtime_publish_error(rt, "invalid slot card configuration");
            break;
        }
        if (cmd->data.apply_machine_config.turbo_speed_count > 0u) {
            runtime_install_turbo_ladder(
                rt,
                cmd->data.apply_machine_config.turbo_speeds,
                cmd->data.apply_machine_config.turbo_speed_count);
        }
        if (!changed) {
            if (cmd->data.apply_machine_config.turbo_speed_count > 0u) {
                runtime_publish_machine(rt);
            }
            break;
        }
        if (!apple2_flush_media(&rt->machine)) {
            runtime_publish_error(rt, "could not flush media before machine reconfiguration");
            break;
        }

        runtime_history_prepare_discontinuity(rt);
        for (slot = 1; slot <= 7; ++slot) {
            runtime_slot_card_type next = config->slot_cards[slot];
            runtime_slot_card_type current = RUNTIME_SLOT_CARD_EMPTY;
            int drive;
            if (rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
                current = RUNTIME_SLOT_CARD_DISKII;
            } else if (rt->machine.slot_type[slot] == SLOT_TYPE_SMARTPORT) {
                current = RUNTIME_SLOT_CARD_SMARTPORT;
            } else if (rt->machine.slot_type[slot] == SLOT_TYPE_MOCKINGBOARD) {
                current = RUNTIME_SLOT_CARD_MOCKINGBOARD;
            }
            if (current == next) {
                continue;
            }
            if (current == RUNTIME_SLOT_CARD_DISKII) {
                for (drive = 0; drive < 2; ++drive) {
                    while (rt->machine.diskii_controller[slot].diskii_drive[drive].images.items > 0u) {
                        if (apple2_disk_eject(&rt->machine, slot, drive) != 0) {
                            break;
                        }
                    }
                }
            }
            apple2_detach_slot_card(&rt->machine, slot);
        }

        apple2_set_model(
            &rt->machine,
            config->apple_model == 1u ? APPLE2_MODEL_II_PLUS : APPLE2_MODEL_IIE_ENHANCED);
        rt->config.apple_model = config->apple_model == 1u ? 1 : 0;
        rt->config.mb_slot = 0;
        for (slot = 1; slot <= 7; ++slot) {
            rt->config.slot_cards[slot] = config->slot_cards[slot];
            switch (config->slot_cards[slot]) {
            case RUNTIME_SLOT_CARD_DISKII:
                (void)apple2_attach_diskii(&rt->machine, slot);
                break;
            case RUNTIME_SLOT_CARD_SMARTPORT:
                (void)apple2_attach_smartport(&rt->machine, slot);
                break;
            case RUNTIME_SLOT_CARD_MOCKINGBOARD:
                (void)apple2_attach_mockingboard(&rt->machine, slot);
                rt->config.mb_slot = slot;
                break;
            case RUNTIME_SLOT_CARD_EMPTY:
            default:
                break;
            }
        }
        rt->config.machine_config = *config;
        apple2_cold_reset(&rt->machine);
        runtime_type_script_stop(rt);
        rt->suppress_execute_bp = false;
        rt->temp_bp_active = false;
        rt->breakpoint_hit_pending = false;
        rt->exec_state = cmd->data.apply_machine_config.resume_running != 0u ?
            RUNTIME_EXEC_RUNNING : RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = rt->exec_state == RUNTIME_EXEC_RUNNING ?
            RUNTIME_STOP_REASON_NONE : RUNTIME_STOP_REASON_RESET;
        if (rt->history != NULL) {
            runtime_history_status st;
            (void)runtime_history_transition_timeline(rt->history);
            runtime_history_get_status(rt->history, &st);
            if (st.available && st.recording) {
                (void)runtime_history_append_marker(
                    rt->history,
                    RUNTIME_HISTORY_MARKER_RESET_COMPLETE,
                    RUNTIME_HISTORY_RESET_MACHINE_CONFIG,
                    0u,
                    apple2_cycles(&rt->machine));
            }
        }
        if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
            runtime_reset_pacer(rt);
        }
        runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
        runtime_publish_cpu(rt, 0u);
        runtime_publish_machine(rt);
        runtime_publish_simple(
            rt,
            rt->exec_state == RUNTIME_EXEC_RUNNING ?
                RUNTIME_EVENT_RUNNING : RUNTIME_EVENT_PAUSED);
        break;
    }
    case RUNTIME_COMMAND_RUN:
        if (rt->inspecting && runtime_inspector_at_live(rt)) {
            break;
        }
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_reset_pacer(rt);
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        break;
    case RUNTIME_COMMAND_PAUSE:
        if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
            runtime_finish_to_instruction_boundary(rt);
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            rt->last_stop_reason = RUNTIME_STOP_REASON_PAUSE_COMMAND;
            runtime_publish_machine(rt);
            runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
            runtime_publish_cpu(rt, 0u);
            runtime_publish_state_changed(
                rt, RUNTIME_STATE_CHANGED_PAUSE, cmd->session_id);
            runtime_publish_presented_frame(rt);
            if (rt->inspecting) {
                rt->machine.video.paint_enabled = true;
            }
        }
        break;
    case RUNTIME_COMMAND_RUN_CYCLES: {
        size_t remaining = cmd->data.run_cycles.count;
        size_t i;

        rt->exec_state = RUNTIME_EXEC_RUNNING;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        for (i = 0; i < remaining; i++) {
            if (!rt->suppress_execute_bp &&
                runtime_at_instruction_boundary(rt) &&
                runtime_breakpoint_matches_pc(rt)) {
                runtime_pause_for_breakpoint(rt);
                return;
            }
            if (!apple2_step_cycle(&rt->machine)) {
                break;
            }
            runtime_produce_audio(rt, 1u);
            runtime_maybe_frame(rt);
            if (runtime_pause_if_breakpoint_pending(rt)) {
                return;
            }
            if (rt->suppress_execute_bp && runtime_at_instruction_boundary(rt)) {
                rt->suppress_execute_bp = false;
            }
        }
        runtime_finish_to_instruction_boundary(rt);
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_RUN_COMPLETE;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUN_COMPLETE);
        runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
        runtime_publish_cpu(rt, 0u);
        runtime_publish_presented_frame(rt);
        if (rt->inspecting) {
            rt->machine.video.paint_enabled = true;
        }
        break;
    }
    case RUNTIME_COMMAND_RUN_INSTRUCTIONS: {
        size_t remaining = cmd->data.run_instructions.count;
        size_t i;

        rt->exec_state = RUNTIME_EXEC_RUNNING;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        for (i = 0; i < remaining; i++) {
            uint64_t c0;
            uint64_t c1;

            if (!rt->suppress_execute_bp &&
                runtime_at_instruction_boundary(rt) &&
                runtime_breakpoint_matches_pc(rt)) {
                runtime_pause_for_breakpoint(rt);
                return;
            }
            c0 = rt->machine.cpu.cpu.cycles;
            if (!apple2_step_instruction(&rt->machine)) {
                break;
            }
            runtime_inspector_after_step(rt);
            c1 = rt->machine.cpu.cpu.cycles;
            if (c1 > c0) {
                runtime_produce_audio(rt, (uint32_t)(c1 - c0));
            }
            runtime_maybe_frame(rt);
            if (runtime_pause_if_breakpoint_pending(rt)) {
                return;
            }
            rt->suppress_execute_bp = false;
        }
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        rt->exec_state = RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = RUNTIME_STOP_REASON_RUN_COMPLETE;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUN_COMPLETE);
        runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
        runtime_publish_cpu(rt, 0u);
        runtime_publish_presented_frame(rt);
        if (rt->inspecting) {
            rt->machine.video.paint_enabled = true;
        }
        break;
    }
    case RUNTIME_COMMAND_STEP_INSTRUCTION:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            if (rt->inspecting && runtime_inspector_at_live(rt)) {
                break;
            }
            (void)runtime_exec_step_instruction(rt);
            runtime_maybe_frame(rt);
            rt->suppress_execute_bp = false;
            if (runtime_inspector_pause_at_live(rt)) {
                break;
            }
            if (!runtime_pause_if_breakpoint_pending(rt)) {
                runtime_pause_for_step(rt);
            }
        }
        break;
    case RUNTIME_COMMAND_STEP_CYCLE:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            (void)apple2_step_cycle(&rt->machine);
            runtime_maybe_frame(rt);
            if (rt->suppress_execute_bp && runtime_at_instruction_boundary(rt)) {
                rt->suppress_execute_bp = false;
            }
            if (!runtime_pause_if_breakpoint_pending(rt)) {
                runtime_pause_for_step(rt);
            }
        }
        break;
    case RUNTIME_COMMAND_STEP_OVER:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            runtime_step_over(rt, alive);
        }
        break;
    case RUNTIME_COMMAND_STEP_OUT:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            runtime_step_out(rt, alive);
        }
        break;
    case RUNTIME_COMMAND_RUN_TO_CURSOR:
        runtime_run_to_cursor(rt, cmd->data.run_to_cursor.address, alive);
        break;
    case RUNTIME_COMMAND_REQUEST_CPU_STATE:
        runtime_publish_cpu(rt, cmd->request_token);
        break;
    case RUNTIME_COMMAND_REQUEST_MACHINE_STATE:
        runtime_publish_machine(rt);
        break;
    case RUNTIME_COMMAND_REQUEST_FRAME:
        runtime_publish_presented_frame(rt);
        break;
    case RUNTIME_COMMAND_SET_CPU_REGISTER:
        runtime_set_register(
            rt, cmd->data.set_cpu_register.reg, cmd->data.set_cpu_register.value);
        break;
    case RUNTIME_COMMAND_REQUEST_MEMORY:
    case RUNTIME_COMMAND_REQUEST_MEMORY_VIEW: {
        runtime_command mem = *cmd;
        mem.data.request_memory.mode =
            (uint8_t)cmd->data.request_memory.mode; /* already uint8 */
        runtime_handle_request_memory(rt, &mem);
        break;
    }
    case RUNTIME_COMMAND_REQUEST_DEBUG_MEMORY:
        runtime_fill_debug_memory(
            rt, cmd->data.request_debug_memory.include_write_history != 0);
        break;
    case RUNTIME_COMMAND_REQUEST_CALL_STACK:
        runtime_publish_call_stack(rt, cmd->request_token);
        break;
    case RUNTIME_COMMAND_WRITE_MEMORY_BYTE:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            runtime_write_byte(
                rt,
                cmd->data.write_memory_byte.address,
                cmd->data.write_memory_byte.value,
                (runtime_memory_mode)cmd->data.write_memory_byte.mode);
            runtime_refresh_display_after_memory_edit(rt);
        }
        break;
    case RUNTIME_COMMAND_WRITE_MEMORY:
        if (rt->exec_state != RUNTIME_EXEC_RUNNING) {
            uint16_t i;
            for (i = 0; i < cmd->data.write_memory.length; i++) {
                runtime_write_byte(
                    rt,
                    (uint16_t)(cmd->data.write_memory.address + i),
                    cmd->data.write_memory.bytes[i],
                    (runtime_memory_mode)cmd->data.write_memory.mode);
            }
            runtime_refresh_display_after_memory_edit(rt);
        }
        break;
    case RUNTIME_COMMAND_SET_EXECUTE_BREAKPOINT:
        runtime_set_execute_breakpoint(rt, cmd);
        break;
    case RUNTIME_COMMAND_CLEAR_BREAKPOINT:
        runtime_clear_breakpoint(rt, cmd);
        break;
    case RUNTIME_COMMAND_CLEAR_ALL_BREAKPOINTS:
        runtime_clear_all_breakpoints(rt);
        break;
    case RUNTIME_COMMAND_SET_BREAKPOINT_ENABLED:
        runtime_set_breakpoint_enabled(rt, cmd);
        break;
    case RUNTIME_COMMAND_CREATE_BREAKPOINT:
        runtime_create_breakpoint(rt, cmd);
        break;
    case RUNTIME_COMMAND_UPDATE_BREAKPOINT:
        runtime_update_breakpoint(rt, cmd);
        break;
    case RUNTIME_COMMAND_DUPLICATE_BREAKPOINT:
        runtime_duplicate_breakpoint(rt, cmd);
        break;
    case RUNTIME_COMMAND_REARM_ONESHOT_BREAKPOINTS:
        runtime_rearm_oneshot_breakpoints(rt);
        break;
    case RUNTIME_COMMAND_REQUEST_BREAKPOINTS:
        runtime_publish_breakpoints(rt);
        break;
    case RUNTIME_COMMAND_KEYBOARD_KEY:
        runtime_inject_host_key(
            rt,
            cmd->data.keyboard_key.key,
            cmd->data.keyboard_key.pressed != 0);
        break;
    case RUNTIME_COMMAND_SET_GAMEPORT:
        apple2_gameport_set_axes(&rt->machine, cmd->data.set_gameport.axis);
        apple2_gameport_set_buttons(&rt->machine, cmd->data.set_gameport.buttons);
        break;
    case RUNTIME_COMMAND_PASTE_TEXT: {
        const char *text = cmd->data.paste_text.text;
        size_t length = cmd->data.paste_text.length;

        if (text == NULL || length == 0u) {
            break;
        }
        if (!apple2_paste_begin(&rt->machine, text, length)) {
            break;
        }
        /* Paste injects keys only — does not change turbo (zip policy). */
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        rt->last_stop_reason = RUNTIME_STOP_REASON_NONE;
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_CYCLE_TURBO_SPEED: {
        uint8_t i;
        uint8_t next = 0;
        for (i = 0; i < rt->turbo_speed_count; i++) {
            if (rt->turbo_speeds[i] == rt->active_turbo_multiplier) {
                next = (uint8_t)((i + 1u) % rt->turbo_speed_count);
                break;
            }
        }
        runtime_set_active_turbo(rt, rt->turbo_speeds[next]);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_SET_TURBO_MULTIPLIER:
        runtime_set_active_turbo(rt, cmd->data.set_turbo_multiplier.multiplier);
        runtime_publish_machine(rt);
        break;
    case RUNTIME_COMMAND_MEDIA_INSERT: {
        const uint8_t slot = cmd->data.media_insert.slot;
        const uint8_t device = cmd->data.media_insert.device;
        const runtime_slot_card_type card_type =
            (runtime_slot_card_type)cmd->data.media_insert.card_type;
        int result = -1;

        if (slot >= 1u && slot <= 7u && device <= 1u) {
            if (card_type == RUNTIME_SLOT_CARD_DISKII) {
                /* Empty slot: attach Disk II (same convenience the old
                   slot-6 mount shortcut had for control mount-disk). */
                if (rt->machine.slot_type[slot] == SLOT_TYPE_EMPTY) {
                    (void)apple2_attach_diskii(&rt->machine, (int)slot);
                }
                if (rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
                    result = apple2_disk_mount(
                        &rt->machine, slot, device, cmd->data.media_insert.path);
                }
            } else if (card_type == RUNTIME_SLOT_CARD_SMARTPORT &&
                       rt->machine.slot_type[slot] == SLOT_TYPE_SMARTPORT) {
                result = apple2_smartport_mount(
                    &rt->machine, slot, device, cmd->data.media_insert.path);
            }
        }
        if (result != 0) {
            runtime_publish_error(rt, "media insert failed");
        }
        runtime_publish_media_changed(
            rt, RUNTIME_MEDIA_CHANGE_INSERT, slot, device, card_type,
            result == 0, cmd->data.media_insert.path);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_MEDIA_EJECT: {
        const uint8_t slot = cmd->data.media_device.slot;
        const uint8_t device = cmd->data.media_device.device;
        int result = -1;

        if (slot >= 1u && slot <= 7u && device <= 1u) {
            if (rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
                result = apple2_disk_eject(&rt->machine, slot, device);
            } else if (rt->machine.slot_type[slot] == SLOT_TYPE_SMARTPORT) {
                result = apple2_smartport_eject(&rt->machine, slot, device);
            }
        }
        if (result != 0) {
            runtime_publish_error(rt, "media eject failed");
        }
        runtime_publish_media_changed(
            rt,
            RUNTIME_MEDIA_CHANGE_EJECT,
            slot,
            device,
            slot <= 7u && rt->machine.slot_type[slot] == SLOT_TYPE_SMARTPORT ?
                RUNTIME_SLOT_CARD_SMARTPORT : RUNTIME_SLOT_CARD_DISKII,
            result == 0,
            NULL);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_MEDIA_SWAP: {
        const uint8_t slot = cmd->data.media_swap.slot;
        const uint8_t device = cmd->data.media_swap.device;
        int32_t param = cmd->data.media_swap.param;
        uint8_t relative = cmd->data.media_swap.relative;
        int result = -1;

        if (param == 0) {
            param = 1;
            relative = 1u;
        }
        if (slot >= 1u && slot <= 7u && device <= 1u &&
            rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
            result = apple2_disk_swap(
                &rt->machine,
                slot,
                device,
                param,
                relative != 0u);
        }
        if (result != 0) {
            runtime_publish_error(rt, "Disk II swap failed");
        } else {
            /* Host options mirror absolute/relative via DISK_SWAP (not MEDIA_CHANGED). */
            runtime_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = RUNTIME_EVENT_DISK_SWAP;
            ev.data.disk_swap.slot = slot;
            ev.data.disk_swap.device = device;
            ev.data.disk_swap.swap_param = param;
            ev.data.disk_swap.swap_relative = relative;
            runtime_publish_event(rt, &ev);
        }
        runtime_publish_media_changed(
            rt, RUNTIME_MEDIA_CHANGE_SWAP, slot, device,
            RUNTIME_SLOT_CARD_DISKII, result == 0, NULL);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_SET_DISK_WRITABLE: {
        const uint8_t slot = cmd->data.disk_writable.slot;
        const uint8_t device = cmd->data.disk_writable.device;
        int result = -1;

        if (slot >= 1u && slot <= 7u && device <= 1u &&
            rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
            result = apple2_disk_set_writable(
                &rt->machine,
                slot,
                device,
                cmd->data.disk_writable.writable != 0u);
        }
        if (result != 0) {
            runtime_publish_error(rt, "Disk II set-writable failed");
        }
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_BOOT_SLOT: {
        const uint8_t slot = cmd->data.boot_slot.slot;
        const bool was_running = rt->exec_state == RUNTIME_EXEC_RUNNING;

        if (slot < 1u || slot > 7u ||
            (rt->machine.slot_type[slot] != SLOT_TYPE_DISKII &&
             rt->machine.slot_type[slot] != SLOT_TYPE_SMARTPORT)) {
            runtime_publish_error(rt, "slot is not bootable");
            break;
        }
        runtime_history_prepare_discontinuity(rt);
        apple2_reset(&rt->machine);
        rt->machine.cpu.cpu.pc = (uint16_t)(0xC000u + ((uint16_t)slot << 8));
        rt->suppress_execute_bp = false;
        rt->temp_bp_active = false;
        rt->breakpoint_hit_pending = false;
        rt->exec_state = was_running ? RUNTIME_EXEC_RUNNING : RUNTIME_EXEC_PAUSED;
        rt->last_stop_reason = was_running ?
            RUNTIME_STOP_REASON_NONE : RUNTIME_STOP_REASON_RESET;
        if (was_running) {
            runtime_reset_pacer(rt);
        }
        runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
        runtime_publish_cpu(rt, 0u);
        runtime_publish_machine(rt);
        break;
    }
    case RUNTIME_COMMAND_SET_DISPLAY_OVERRIDE:
        apple2_video_set_display_override(
            &rt->machine,
            cmd->data.set_display_override.enabled != 0u,
            cmd->data.set_display_override.flags);
        if (runtime_paint_presentation_scratch(rt)) {
            runtime_publish_argb_pixels(
                rt, rt->presentation_scratch,
                RUNTIME_FRAME_PUBLISH_HOST_ONLY, 0u);
        }
        break;
    case RUNTIME_COMMAND_SET_VIDEO_DISPLAY:
        rt->config.video_colour = cmd->data.set_video_display.colour != 0u;
        rt->config.video_phosphor = cmd->data.set_video_display.phosphor;
        apple2_video_set_monitor(
            &rt->machine,
            rt->config.video_colour,
            (apple2_video_phosphor)rt->config.video_phosphor);
        if (runtime_paint_presentation_scratch(rt)) {
            runtime_publish_argb_pixels(
                rt, rt->presentation_scratch,
                RUNTIME_FRAME_PUBLISH_HOST_ONLY, 0u);
        }
        break;

    /* ---- History C4a: info / record on|off / clear ---- */
    case RUNTIME_COMMAND_HISTORY_INFO:
        runtime_publish_history_status(rt, cmd->request_token);
        break;

    case RUNTIME_COMMAND_HISTORY_RECORD: {
        uint64_t cycle = apple2_cycles(&rt->machine);
        if (rt->history != NULL) {
            if (cmd->data.history_record.enabled != 0u) {
                (void)runtime_history_resume(rt->history, cycle);
                /* Explicit resume cancels "paused for max" unless still on max
                   with policy — re-apply so max stays history-free. */
                rt->history_paused_for_max = false;
                if (runtime_turbo_is_free_run(rt) && rt->history_off_on_max) {
                    (void)runtime_history_stop(rt->history, cycle);
                    rt->history_paused_for_max = true;
                }
            } else {
                (void)runtime_history_stop(rt->history, cycle);
                rt->history_paused_for_max = false;
            }
            runtime_history_sync_observer(rt);
        }
        runtime_publish_history_status(rt, cmd->request_token);
        break;
    }

    case RUNTIME_COMMAND_INSPECTOR_SET_ENABLED: {
        runtime_inspector_set_enabled(rt, cmd->data.inspector_set_enabled.enabled != 0u);
        runtime_history_sync_observer(rt);
        runtime_publish_history_status(rt, cmd->request_token);
        break;
    }

    case RUNTIME_COMMAND_INSPECTOR_ENTER: {
        runtime_inspector_enter_status st;

        if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
            runtime_finish_to_instruction_boundary(rt);
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            rt->last_stop_reason = RUNTIME_STOP_REASON_PAUSE_COMMAND;
            runtime_publish_machine(rt);
            runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
            runtime_publish_cpu(rt, 0u);
        }
        st = runtime_inspector_enter(rt);
        if (st != RUNTIME_INSPECTOR_ENTER_OK) {
            runtime_inspector_reattach_live_hooks(rt);
            apple2_set_replay_sealed(&rt->machine, false);
        } else {
            runtime_inspector_publish_head(rt);
            runtime_publish_state_changed(
                rt, RUNTIME_STATE_CHANGED_INSPECTOR_ENTER, cmd->session_id);
        }
        runtime_publish_inspector_mode(rt, cmd->request_token, 0u, st);
        break;
    }

    case RUNTIME_COMMAND_INSPECTOR_LEAVE: {
        runtime_inspector_leave(rt);
        runtime_inspector_reattach_live_hooks(rt);
        runtime_inspector_publish_head(rt);
        runtime_publish_state_changed(
            rt, RUNTIME_STATE_CHANGED_INSPECTOR_LEAVE, cmd->session_id);
        runtime_publish_inspector_mode(
            rt, cmd->request_token, 1u, RUNTIME_INSPECTOR_ENTER_OK);
        break;
    }

    case RUNTIME_COMMAND_SET_HISTORY_OFF_ON_MAX: {
        bool enable = cmd->data.set_history_off_on_max.enabled != 0u;
        rt->history_off_on_max = enable;
        if (runtime_turbo_is_free_run(rt)) {
            if (enable) {
                runtime_history_apply_max_policy(rt, true, false);
            } else if (rt->history_paused_for_max) {
                /* Policy turned off while on max: restore recording. */
                runtime_history_apply_max_policy(rt, false, true);
            }
        }
        break;
    }

    case RUNTIME_COMMAND_HISTORY_CLEAR: {
        uint64_t cycle = apple2_cycles(&rt->machine);
        if (rt->history != NULL) {
            runtime_history_prepare_discontinuity(rt);
            (void)runtime_history_clear(rt->history, cycle);
            runtime_inspector_on_history_invalidate(rt);
            runtime_history_sync_observer(rt);
        }
        runtime_publish_history_status(rt, cmd->request_token);
        break;
    }

    case RUNTIME_COMMAND_HISTORY_FIND:
        runtime_history_find_command(rt, cmd);
        break;
    case RUNTIME_COMMAND_HISTORY_NEXT:
        runtime_history_next_command(rt, cmd);
        break;
    case RUNTIME_COMMAND_HISTORY_READ:
        runtime_history_read_command(rt, cmd);
        break;
    case RUNTIME_COMMAND_HISTORY_CLOSE: {
        runtime_session *session =
            runtime_session_resolve(rt, cmd->data.history_close.session_id);
        if (session != NULL &&
            (cmd->data.history_close.cursor == 0u ||
             cmd->data.history_close.cursor == session->history_cursor.id)) {
            memset(
                &session->history_cursor,
                0,
                sizeof(session->history_cursor));
        }
        runtime_publish_history_rpc_status(
            rt, cmd->request_token, RUNTIME_HISTORY_RPC_OK);
        break;
    }
    case RUNTIME_COMMAND_SESSION_OPEN: {
        runtime_session_kind kind =
            (runtime_session_kind)cmd->data.session_open.kind;
        runtime_session *session = runtime_session_allocate(
            rt, kind, cmd->data.session_open.endpoint_epoch);
        if (session == NULL) {
            runtime_session_status status =
                (kind != RUNTIME_SESSION_KIND_UI &&
                 kind != RUNTIME_SESSION_KIND_CONTROL)
                    ? RUNTIME_SESSION_BAD_ARGS
                    : RUNTIME_SESSION_FULL;
            runtime_publish_session_response(
                rt, cmd->request_token, status, 0u, kind);
        } else {
            runtime_publish_session_response(
                rt,
                cmd->request_token,
                RUNTIME_SESSION_OK,
                session->id,
                session->kind);
        }
        break;
    }
    case RUNTIME_COMMAND_SESSION_CLOSE: {
        uint32_t session_id = cmd->data.session_close.session_id;
        runtime_session_kind kind = RUNTIME_SESSION_KIND_NONE;
        runtime_session *session = runtime_session_lookup(rt, session_id);
        if (session != NULL) {
            kind = session->kind;
        }
        if (session_id == 0u) {
            runtime_publish_session_response(
                rt,
                cmd->request_token,
                RUNTIME_SESSION_BAD_ARGS,
                0u,
                RUNTIME_SESSION_KIND_NONE);
        } else if (!runtime_session_release(rt, session_id)) {
            runtime_publish_session_response(
                rt,
                cmd->request_token,
                RUNTIME_SESSION_NOT_FOUND,
                session_id,
                kind);
        } else {
            runtime_publish_session_response(
                rt,
                cmd->request_token,
                RUNTIME_SESSION_OK,
                session_id,
                kind);
        }
        break;
    }

    case RUNTIME_COMMAND_INSPECTOR_LAND:
        if (rt->inspecting) {
            if (runtime_inspector_land(rt, cmd->data.inspector_land.cycle)) {
                runtime_inspector_publish_head(rt);
                runtime_publish_state_changed(
                    rt,
                    RUNTIME_STATE_CHANGED_INSPECTOR_LAND,
                    cmd->session_id);
            }
        }
        break;
    case RUNTIME_COMMAND_INSPECTOR_LAND_TO_CYCLE:
        if (rt->inspecting) {
            /* Publish once after exact land even on partial (best-effort focus). */
            (void)runtime_inspector_land_to_cycle(
                rt, cmd->data.inspector_land_to_cycle.cycle);
            runtime_inspector_publish_head(rt);
            runtime_publish_state_changed(
                rt,
                RUNTIME_STATE_CHANGED_INSPECTOR_LAND,
                cmd->session_id);
        }
        break;
    case RUNTIME_COMMAND_INSPECTOR_FRAME_STEP:
        if (rt->inspecting) {
            if (runtime_inspector_frame_step(rt, (int)cmd->data.inspector_frame_step.direction)) {
                runtime_inspector_publish_head(rt);
                runtime_publish_state_changed(
                    rt,
                    RUNTIME_STATE_CHANGED_INSPECTOR_LAND,
                    cmd->session_id);
            }
        }
        break;

    case RUNTIME_COMMAND_SAVE_STATE:
        runtime_save_state(rt, cmd);
        break;
    case RUNTIME_COMMAND_LOAD_STATE:
        runtime_load_state(rt, cmd);
        break;
    case RUNTIME_COMMAND_LOAD_BIN:
        runtime_load_bin(rt, cmd);
        break;
    case RUNTIME_COMMAND_ASSEMBLE_FILE:
        runtime_assemble_file_command(rt, cmd);
        break;
    case RUNTIME_COMMAND_SAVE_BIN:
        runtime_save_bin(rt, cmd);
        break;

    default:
        /* Unknown / unsupported command: ignore. */
        break;
    }
}

static void runtime_free_run_batch(runtime *rt)
{
    uint32_t i;

    /* Max: instruction quanta + 60 Hz paint (S2). Finite: Φ0 beam path.
       Time travel always uses the cycle path so the input log applies. */
    if (runtime_turbo_is_free_run(rt) && !rt->inspecting) {
        runtime_free_run_max_quantum(rt);
        return;
    }

    if (rt->inspecting) {
        rt->machine.video.paint_enabled = false;
    }

    for (i = 0; i < RUNTIME_RUN_BATCH_CYCLES; i++) {
        uint64_t c0;

        if (rt->inspecting && runtime_inspector_pause_at_live(rt)) {
            return;
        }
        if (!rt->suppress_execute_bp &&
            runtime_at_instruction_boundary(rt) &&
            (rt->breakpoint_count > 0 || rt->temp_bp_active) &&
            runtime_breakpoint_matches_pc(rt)) {
            if (rt->temp_bp_active &&
                rt->temp_bp_skip_current &&
                rt->machine.cpu.cpu.pc == rt->temp_bp_address) {
                rt->temp_bp_skip_current = false;
            } else {
                runtime_pause_for_breakpoint(rt);
                return;
            }
        }
        c0 = apple2_cycles(&rt->machine);
        if (!apple2_step_cycle(&rt->machine)) {
            rt->exec_state = RUNTIME_EXEC_PAUSED;
            rt->last_stop_reason = RUNTIME_STOP_REASON_ERROR;
            runtime_publish_error(rt, "step_cycle failed");
            runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
            return;
        }
        if (rt->inspecting) {
            runtime_inspector_apply_logged_inputs(
                rt, &rt->machine, c0 + 1u, apple2_cycles(&rt->machine));
            if (runtime_inspector_at_live(rt)) {
                (void)runtime_inspector_restore_live(rt);
                (void)runtime_inspector_pause_at_live(rt);
                return;
            }
            runtime_inspector_sync_focus(rt);
        } else {
            /* One cycle at a time so $C030 toggles land on the right samples. */
            runtime_produce_audio(rt, 1u);
            runtime_type_script_tick(rt, 1u);
        }
        runtime_maybe_frame(rt);
        if (runtime_pause_if_breakpoint_pending(rt)) {
            return;
        }
        if (rt->suppress_execute_bp && runtime_at_instruction_boundary(rt)) {
            rt->suppress_execute_bp = false;
        }
        if (!rt->inspecting && rt->machine.instruction_complete) {
            runtime_inspector_after_step(rt);
        }
    }
}

static bool runtime_command_is_inspector_land(const runtime_command *cmd)
{
    return cmd != NULL &&
        (cmd->type == RUNTIME_COMMAND_INSPECTOR_LAND ||
         cmd->type == RUNTIME_COMMAND_INSPECTOR_LAND_TO_CYCLE);
}

static bool runtime_command_preempts_inspector_land(const runtime_command *cmd)
{
    return cmd != NULL &&
        (cmd->type == RUNTIME_COMMAND_QUIT ||
         cmd->type == RUNTIME_COMMAND_INSPECTOR_LEAVE);
}

/* Keep only the latest land. Quit / leave-inspector drop the backlog. */
static void runtime_coalesce_inspector_lands(
    runtime *rt,
    runtime_command *cmd,
    runtime_command *deferred,
    bool *has_deferred)
{
    runtime_command extra;

    if (rt == NULL || cmd == NULL || deferred == NULL || has_deferred == NULL) {
        return;
    }
    if (!runtime_command_is_inspector_land(cmd)) {
        return;
    }
    while (message_queue_try_pop(rt->command_queue, &extra)) {
        if (runtime_command_is_inspector_land(&extra)) {
            *cmd = extra;
            continue;
        }
        if (runtime_command_preempts_inspector_land(&extra)) {
            *cmd = extra;
            while (message_queue_try_pop(rt->command_queue, &extra)) {
                if (runtime_command_is_inspector_land(&extra)) {
                    continue;
                }
                *deferred = extra;
                *has_deferred = true;
                break;
            }
            return;
        }
        *deferred = extra;
        *has_deferred = true;
        return;
    }
}

int runtime_thread_main(void *userdata)
{
    runtime *rt = (runtime *)userdata;
    bool alive = true;
    runtime_command command;

    if (rt == NULL) {
        return 1;
    }

    rt->symbols = symbol_table_create();
    if (rt->symbols == NULL) {
        runtime_publish_error(rt, "symbol table initialization failed");
        runtime_publish_simple(rt, RUNTIME_EVENT_STOPPED);
        return 1;
    }

    if (!apple2_init(&rt->machine)) {
        runtime_publish_error(rt, "apple2_init failed");
        runtime_publish_simple(rt, RUNTIME_EVENT_STOPPED);
        symbol_table_destroy(rt->symbols);
        rt->symbols = NULL;
        return 1;
    }
    apple2_video_set_monitor(
        &rt->machine,
        rt->config.video_colour,
        (apple2_video_phosphor)rt->config.video_phosphor);
    apple2_set_memory_access_callback(&rt->machine, runtime_on_memory_access, rt);
    apple2_set_model(
        &rt->machine,
        rt->config.apple_model == 1 ? APPLE2_MODEL_II_PLUS : APPLE2_MODEL_IIE_ENHANCED);
    {
        int slot;
        for (slot = 1; slot <= 7; ++slot) {
            apple2_detach_slot_card(&rt->machine, slot);
        }
        for (slot = 1; slot <= 7; ++slot) {
            switch (rt->config.slot_cards[slot]) {
            case RUNTIME_SLOT_CARD_DISKII:
                (void)apple2_attach_diskii(&rt->machine, slot);
                break;
            case RUNTIME_SLOT_CARD_SMARTPORT:
                (void)apple2_attach_smartport(&rt->machine, slot);
                break;
            case RUNTIME_SLOT_CARD_MOCKINGBOARD:
                (void)apple2_attach_mockingboard(&rt->machine, slot);
                break;
            case RUNTIME_SLOT_CARD_EMPTY:
            default:
                break;
            }
        }
    }
    {
        int i;
        for (i = 0; i < rt->diskii_mount_count; i++) {
            int slot = rt->diskii_slots[i];
            int drive = rt->diskii_drives[i];
            const char *path = rt->diskii_paths[i];
            if (path == NULL || path[0] == '\0') {
                continue;
            }
            if (slot < 1 || slot > 7 || drive < 0 || drive > 1) {
                continue;
            }
            if (rt->machine.slot_type[slot] == SLOT_TYPE_DISKII) {
                (void)apple2_disk_mount(&rt->machine, slot, drive, path);
            }
        }
        /* Multi-image queues: start each drive on the first image (boot disk). */
        for (i = 1; i <= 7; i++) {
            int d;
            if (!rt->machine.diskii_present[i]) {
                continue;
            }
            for (d = 0; d < 2; d++) {
                DISKII_DRIVE *dd = &rt->machine.diskii_controller[i].diskii_drive[d];
                if (dd->images.items > 0u) {
                    (void)apple2_disk_select_image(&rt->machine, i, d, 0);
                }
            }
        }
        for (i = 0; i < rt->smartport_mount_count; i++) {
            int slot = rt->smartport_slots[i];
            int unit = rt->smartport_units[i];
            const char *path = rt->smartport_paths[i];
            if (path == NULL || path[0] == '\0') {
                continue;
            }
            if (slot < 1 || slot > 7) {
                continue;
            }
            if (rt->machine.slot_type[slot] == SLOT_TYPE_SMARTPORT) {
                (void)apple2_smartport_mount(&rt->machine, slot, unit, path);
            }
        }
    }
    apple2_reset(&rt->machine);
    if (rt->config.smartport_boot_slot != 0) {
        int slot = rt->config.smartport_boot_slot;
        if (rt->machine.slot_type[slot] != SLOT_TYPE_SMARTPORT) {
            runtime_publish_error(rt, "configured SmartPort boot slot has no SmartPort card");
        } else if (!sp_unit_mounted(&rt->machine.sp_device[slot], 0)) {
            runtime_publish_error(rt, "configured SmartPort boot slot has no mounted unit 0");
        } else {
            rt->machine.cpu.cpu.pc = (uint16_t)(0xC000u + ((uint16_t)slot << 8));
        }
    }
    rt->machine_ready = true;
    runtime_apply_turbo_video_policy(rt, false);
    if (rt->config.inspector) {
        runtime_inspector_set_enabled(rt, true);
    }
    if (runtime_turbo_is_free_run(rt)) {
        runtime_history_apply_max_policy(rt, true, false);
    }
    rt->exec_state = RUNTIME_EXEC_PAUSED;
    rt->last_stop_reason = RUNTIME_STOP_REASON_RESET;

    /* Install flight-recorder observer when arena is available+recording. */
    runtime_history_sync_observer(rt);
    if (rt->history != NULL) {
        runtime_history_status st;
        runtime_history_get_status(rt->history, &st);
        if (st.available && st.recording) {
            (void)runtime_history_append_marker(
                rt->history,
                RUNTIME_HISTORY_MARKER_RESET_COMPLETE,
                RUNTIME_HISTORY_RESET_INITIAL_STARTUP,
                0u,
                apple2_cycles(&rt->machine));
        }
    }

    /* [DEBUG] break.* from product INI (P4e). Same path as create_breakpoint. */
    if (runtime_load_breakpoints_from_ini(rt)) {
        runtime_refresh_rw_breakpoint_flag(rt);
        runtime_publish_breakpoints(rt);
    }

    runtime_publish_simple(rt, RUNTIME_EVENT_STARTED);
    runtime_publish_simple(rt, RUNTIME_EVENT_RESET_COMPLETE);
    runtime_publish_simple(rt, RUNTIME_EVENT_PAUSED);
    runtime_publish_cpu(rt, 0u);
    runtime_publish_machine(rt);

    if (rt->config.start_running) {
        rt->exec_state = RUNTIME_EXEC_RUNNING;
        runtime_reset_pacer(rt);
        runtime_publish_simple(rt, RUNTIME_EVENT_RUNNING);
    }

    {
        runtime_command deferred;
        bool has_deferred = false;

        while (alive && !runtime_quit_requested(rt)) {
            for (;;) {
                if (runtime_quit_requested(rt)) {
                    alive = false;
                    break;
                }
                if (has_deferred) {
                    command = deferred;
                    has_deferred = false;
                } else if (!message_queue_try_pop(rt->command_queue, &command)) {
                    break;
                }
                runtime_coalesce_inspector_lands(
                    rt, &command, &deferred, &has_deferred);
                runtime_process_command(rt, &command, &alive);
                if (!alive) {
                    break;
                }
            }
            if (!alive) {
                break;
            }
            if (rt->exec_state == RUNTIME_EXEC_RUNNING) {
                if (rt->inspecting && runtime_inspector_pause_at_live(rt)) {
                    /* already at live */
                } else {
                    runtime_free_run_batch(rt);
                }
            } else if (!has_deferred) {
                if (!message_queue_wait_pop_timeout(
                        rt->command_queue, &command, 10u)) {
                    continue;
                }
                if (runtime_quit_requested(rt) &&
                    command.type != RUNTIME_COMMAND_QUIT) {
                    alive = false;
                    break;
                }
                runtime_coalesce_inspector_lands(
                    rt, &command, &deferred, &has_deferred);
                runtime_process_command(rt, &command, &alive);
            }
        }
    }

    runtime_publish_simple(rt, RUNTIME_EVENT_STOPPED);
    if (!apple2_flush_media(&rt->machine)) {
        runtime_publish_error(rt, "failed to flush media during shutdown");
    }
    apple2_shutdown(&rt->machine);
    rt->machine_ready = false;
    symbol_table_destroy(rt->symbols);
    rt->symbols = NULL;
    return 0;
}

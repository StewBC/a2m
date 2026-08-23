#include "runtime_history.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    HISTORY_EXEC_HEADER_SIZE = 22,
    HISTORY_ACCESS_SIZE = 6,
    HISTORY_MAX_RECORD_SIZE =
        HISTORY_EXEC_HEADER_SIZE +
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD * HISTORY_ACCESS_SIZE,
    HISTORY_TAG_KIND_MASK = 0x03,
    HISTORY_TAG_LENGTH_SHIFT = 2,
    HISTORY_TAG_LENGTH_MASK = 0x0c,
    HISTORY_TAG_PARTIAL = 0x10,
    HISTORY_TAG_ACCESS_TRUNCATED = 0x20,
    HISTORY_TAG_TIMING_TRUNCATED = 0x40
};

typedef struct runtime_history_block {
    uint64_t epoch;
    uint64_t first_id;
    uint64_t last_id;
    uint64_t base_cycle;
    uint32_t timeline;
    uint32_t used;
    uint32_t record_count;
    uint32_t partial_count;
    uint8_t occupied;
    uint8_t sealed;
} runtime_history_block;

struct runtime_history {
    void *allocation;
    size_t allocation_size;
    runtime_history_free_fn allocation_free;
    void *allocator_user;
    runtime_history_block *blocks;
    uint8_t *arena;
    size_t block_size;
    size_t block_count;
    size_t current_block;
    size_t active_offset;
    runtime_history_block *active_block;
    uint8_t *active_header;
    uint8_t *active_access_cursor;
    uint64_t active_id;
    uint64_t active_start_cycle;
    uint64_t epoch;
    uint64_t next_id;
    uint64_t wrap_count;
    uint64_t truncated_accesses;
    size_t requested_bytes;
    uint32_t timeline;
    runtime_history_unavailable_reason unavailable_reason;
    uint8_t available;
    uint8_t recording;
    uint8_t has_current_block;
    uint8_t has_active_record;
    uint64_t retain_oldest_id;
};

static void *history_default_alloc(size_t size, void *user) {
    (void)user;
    return malloc(size);
}

static void history_default_free(void *ptr, void *user) {
    (void)user;
    free(ptr);
}

static size_t history_align_up(size_t value, size_t alignment) {
    size_t mask = alignment - 1u;
    if (value > SIZE_MAX - mask) {
        return SIZE_MAX;
    }
    return (value + mask) & ~mask;
}

/* memcpy lets little-endian targets use efficient unaligned halfword/word
   accesses while the explicit fallback preserves the arena's LE encoding. */
static void history_write_u16(uint8_t *p, uint16_t value) {
#if defined(_WIN32) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &value, sizeof(value));
#else
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)(value >> 8);
#endif
}

static void history_write_u32(uint8_t *p, uint32_t value) {
#if defined(_WIN32) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &value, sizeof(value));
#else
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)(value >> 24);
#endif
}

static uint16_t history_read_u16(const uint8_t *p) {
#if defined(_WIN32) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
#else
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
#endif
}

static uint32_t history_read_u32(const uint8_t *p) {
#if defined(_WIN32) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
#else
    return (uint32_t)p[0] |
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
#endif
}

static uint8_t *history_block_bytes(
    const runtime_history *history,
    size_t block_index) {
    return history->arena + block_index * history->block_size;
}

static size_t history_record_size_from_bytes(
    const uint8_t *bytes,
    size_t remaining) {
    uint8_t access_count;
    size_t size;

    if (remaining < HISTORY_EXEC_HEADER_SIZE) {
        return 0u;
    }
    access_count = bytes[20];
    if (access_count > RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD) {
        return 0u;
    }
    size = HISTORY_EXEC_HEADER_SIZE + (size_t)access_count * HISTORY_ACCESS_SIZE;
    return size <= remaining ? size : 0u;
}

static void history_seal_current_block(runtime_history *history) {
    if (history->has_current_block) {
        history->blocks[history->current_block].sealed = 1u;
    }
}

static bool history_advance_block(runtime_history *history, uint64_t cycle) {
    size_t next;
    runtime_history_block *block;

    if (!history->available || history->block_count == 0u) {
        return false;
    }
    if (history->has_current_block) {
        history_seal_current_block(history);
        next = (history->current_block + 1u) % history->block_count;
    } else {
        next = 0u;
    }
    block = &history->blocks[next];
    if (block->occupied) {
        history->wrap_count++;
    }
    memset(block, 0, sizeof(*block));
    block->occupied = 1u;
    block->epoch = history->epoch;
    block->timeline = history->timeline;
    block->base_cycle = cycle;
    history->current_block = next;
    history->has_current_block = 1u;
    return true;
}

static bool history_ensure_space(
    runtime_history *history,
    uint64_t cycle,
    size_t required) {
    runtime_history_block *block;
    uint64_t delta;

    if (!history->available || required > history->block_size) {
        return false;
    }
    if (!history->has_current_block &&
        !history_advance_block(history, cycle)) {
        return false;
    }
    block = &history->blocks[history->current_block];
    delta = cycle >= block->base_cycle ? cycle - block->base_cycle : UINT64_MAX;
    if (block->epoch != history->epoch ||
        block->timeline != history->timeline ||
        delta > UINT32_MAX ||
        required > history->block_size - block->used) {
        if (!history_advance_block(history, cycle)) {
            return false;
        }
    }
    return true;
}

static uint8_t *history_active_bytes(runtime_history *history) {
    if (!history->has_active_record) {
        return NULL;
    }
    return history->active_header;
}

static bool history_decode_at(
    const runtime_history *history,
    size_t block_index,
    size_t offset,
    uint64_t id,
    runtime_history_record *out_record,
    size_t *out_size) {
    const runtime_history_block *block;
    const uint8_t *bytes;
    uint8_t tag;
    uint8_t stored_access_count;
    uint8_t fetch_count = 0u;
    runtime_history_access stored_accesses[
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD];
    runtime_history_access fetches[3];
    size_t size;
    size_t i;

    if (history == NULL || out_record == NULL ||
        block_index >= history->block_count) {
        return false;
    }
    block = &history->blocks[block_index];
    if (!block->occupied || offset > block->used) {
        return false;
    }
    bytes = history_block_bytes(history, block_index) + offset;
    size = history_record_size_from_bytes(bytes, block->used - offset);
    if (size == 0u) {
        return false;
    }
    memset(out_record, 0, sizeof(*out_record));
    tag = bytes[21];
    stored_access_count = bytes[20];
    out_record->epoch = block->epoch;
    out_record->id = id;
    out_record->timeline = block->timeline;
    out_record->machine_cycle =
        block->base_cycle + (uint64_t)history_read_u32(bytes + 0);
    out_record->kind =
        (runtime_history_record_kind)(tag & HISTORY_TAG_KIND_MASK);
    out_record->partial = (tag & HISTORY_TAG_PARTIAL) != 0u;
    out_record->access_truncated =
        (tag & HISTORY_TAG_ACCESS_TRUNCATED) != 0u;
    out_record->timing_truncated =
        (tag & HISTORY_TAG_TIMING_TRUNCATED) != 0u;

    if (out_record->kind == RUNTIME_HISTORY_RECORD_MARKER) {
        out_record->marker_kind = history_read_u16(bytes + 4);
        out_record->marker_arg0 = history_read_u32(bytes + 6);
        out_record->marker_arg1 = history_read_u32(bytes + 10);
    } else {
        out_record->pc = history_read_u16(bytes + 4);
        out_record->a = bytes[6];
        out_record->x = bytes[7];
        out_record->y = bytes[8];
        out_record->sp = bytes[9];
        out_record->p = bytes[10];
        out_record->opcode = bytes[11];
        out_record->operand1 = bytes[12];
        out_record->operand2 = bytes[13];
        out_record->instruction_length =
            (uint8_t)((tag & HISTORY_TAG_LENGTH_MASK) >>
                      HISTORY_TAG_LENGTH_SHIFT);
    }
    for (i = 0u; i < stored_access_count; ++i) {
        const uint8_t *access =
            bytes + HISTORY_EXEC_HEADER_SIZE + i * HISTORY_ACCESS_SIZE;
        stored_accesses[i].address = history_read_u16(access + 0);
        stored_accesses[i].cycle_offset = history_read_u16(access + 2);
        stored_accesses[i].value = access[4];
        stored_accesses[i].kind =
            (c6510_bus_access_kind)access[5];
    }
    if (out_record->kind == RUNTIME_HISTORY_RECORD_INSTRUCTION) {
        if (out_record->instruction_length >= 1u) {
            fetches[fetch_count].address = out_record->pc;
            fetches[fetch_count].cycle_offset = history_read_u16(bytes + 14);
            fetches[fetch_count].value = out_record->opcode;
            fetches[fetch_count].kind = C6510_BUS_ACCESS_OPCODE_FETCH;
            fetch_count++;
        }
        if (out_record->instruction_length >= 2u) {
            fetches[fetch_count].address = (uint16_t)(out_record->pc + 1u);
            fetches[fetch_count].cycle_offset = history_read_u16(bytes + 16);
            fetches[fetch_count].value = out_record->operand1;
            fetches[fetch_count].kind = C6510_BUS_ACCESS_OPERAND_READ;
            fetch_count++;
        }
        if (out_record->instruction_length >= 3u) {
            fetches[fetch_count].address = (uint16_t)(out_record->pc + 2u);
            fetches[fetch_count].cycle_offset = history_read_u16(bytes + 18);
            fetches[fetch_count].value = out_record->operand2;
            fetches[fetch_count].kind = C6510_BUS_ACCESS_OPERAND_READ;
            fetch_count++;
        }
    }
    {
        size_t stored_index = 0u;
        size_t fetch_index = 0u;
        size_t output_index = 0u;
        while (stored_index < stored_access_count || fetch_index < fetch_count) {
            bool take_fetch =
                fetch_index < fetch_count &&
                (stored_index >= stored_access_count ||
                 fetches[fetch_index].cycle_offset <=
                     stored_accesses[stored_index].cycle_offset);
            out_record->accesses[output_index++] =
                take_fetch ? fetches[fetch_index++] :
                             stored_accesses[stored_index++];
        }
        out_record->access_count = (uint8_t)output_index;
    }
    if (out_size != NULL) {
        *out_size = size;
    }
    return true;
}

static bool history_find_record(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    size_t *out_block,
    size_t *out_offset) {
    size_t block_index;

    if (history == NULL || !history->available || epoch != history->epoch ||
        id == 0u) {
        return false;
    }
    for (block_index = 0u; block_index < history->block_count; ++block_index) {
        const runtime_history_block *block = &history->blocks[block_index];
        size_t offset = 0u;
        uint64_t current_id;
        uint32_t record_index;

        if (!block->occupied || block->epoch != epoch ||
            id < block->first_id || id > block->last_id) {
            continue;
        }
        current_id = block->first_id;
        for (record_index = 0u;
             record_index < block->record_count;
             ++record_index, ++current_id) {
            const uint8_t *bytes = history_block_bytes(history, block_index) + offset;
            size_t size = history_record_size_from_bytes(bytes, block->used - offset);
            if (size == 0u) {
                return false;
            }
            if (current_id == id) {
                if (out_block != NULL) {
                    *out_block = block_index;
                }
                if (out_offset != NULL) {
                    *out_offset = offset;
                }
                return true;
            }
            offset += size;
        }
        return false;
    }
    return false;
}

runtime_history *runtime_history_create(size_t requested_bytes) {
    return runtime_history_create_ex(
        requested_bytes,
        RUNTIME_HISTORY_DEFAULT_BLOCK_SIZE,
        NULL);
}

runtime_history *runtime_history_create_ex(
    size_t requested_bytes,
    size_t block_size,
    const runtime_history_allocator *allocator) {
    runtime_history *history;
    runtime_history_alloc_fn alloc_fn = history_default_alloc;
    runtime_history_free_fn free_fn = history_default_free;
    void *allocator_user = NULL;
    size_t block_count;
    size_t descriptor_bytes;

    history = (runtime_history *)calloc(1u, sizeof(*history));
    if (history == NULL) {
        return NULL;
    }
    history->requested_bytes = requested_bytes;
    history->epoch = 1u;
    history->next_id = 1u;
    history->recording = requested_bytes != 0u ? 1u : 0u;
    history->unavailable_reason =
        requested_bytes == 0u ?
            RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG :
            RUNTIME_HISTORY_UNAVAILABLE_NONE;

    if (requested_bytes == 0u) {
        return history;
    }
    if (allocator != NULL) {
        if (allocator->alloc != NULL) {
            alloc_fn = allocator->alloc;
        }
        if (allocator->free != NULL) {
            free_fn = allocator->free;
        }
        allocator_user = allocator->user;
    }
    history->allocation_free = free_fn;
    history->allocator_user = allocator_user;

    if (block_size < HISTORY_MAX_RECORD_SIZE ||
        requested_bytes < block_size + sizeof(runtime_history_block)) {
        history->recording = 0u;
        history->unavailable_reason =
            RUNTIME_HISTORY_UNAVAILABLE_INVALID_CAPACITY;
        return history;
    }
    block_count =
        requested_bytes / (block_size + sizeof(runtime_history_block));
    while (block_count > 0u) {
        descriptor_bytes = history_align_up(
            block_count * sizeof(runtime_history_block),
            sizeof(uint64_t));
        if (descriptor_bytes != SIZE_MAX &&
            descriptor_bytes <= requested_bytes &&
            block_count <= (requested_bytes - descriptor_bytes) / block_size) {
            break;
        }
        block_count--;
    }
    if (block_count == 0u) {
        history->recording = 0u;
        history->unavailable_reason =
            RUNTIME_HISTORY_UNAVAILABLE_INVALID_CAPACITY;
        return history;
    }

    history->allocation = alloc_fn(requested_bytes, allocator_user);
    if (history->allocation == NULL) {
        history->recording = 0u;
        history->unavailable_reason =
            RUNTIME_HISTORY_UNAVAILABLE_ALLOCATION_FAILED;
        return history;
    }
    history->allocation_size = requested_bytes;
    history->block_size = block_size;
    history->block_count = block_count;
    history->blocks = (runtime_history_block *)history->allocation;
    descriptor_bytes = history_align_up(
        block_count * sizeof(runtime_history_block),
        sizeof(uint64_t));
    history->arena = (uint8_t *)history->allocation + descriptor_bytes;
    memset(history->blocks, 0, block_count * sizeof(runtime_history_block));
    history->available = 1u;
    return history;
}

void runtime_history_destroy(runtime_history *history) {
    if (history == NULL) {
        return;
    }
    if (history->allocation != NULL && history->allocation_free != NULL) {
        history->allocation_free(history->allocation, history->allocator_user);
    }
    free(history);
}

void runtime_history_get_status(
    const runtime_history *history,
    runtime_history_status *out_status) {
    size_t i;

    if (out_status == NULL) {
        return;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (history == NULL) {
        out_status->unavailable_reason =
            RUNTIME_HISTORY_UNAVAILABLE_ALLOCATION_FAILED;
        return;
    }
    out_status->available = history->available != 0u;
    out_status->recording = history->recording != 0u;
    out_status->unavailable_reason = history->unavailable_reason;
    out_status->requested_bytes = history->requested_bytes;
    out_status->capacity_bytes = history->block_count * history->block_size;
    out_status->epoch = history->epoch;
    out_status->timeline = history->timeline;
    out_status->wrap_count = history->wrap_count;
    out_status->truncated_accesses = history->truncated_accesses;
    /* O(blocks), never O(records): telemetry calls this while recording. */
    for (i = 0u; i < history->block_count; ++i) {
        const runtime_history_block *block = &history->blocks[i];
        if (!block->occupied || block->epoch != history->epoch) {
            continue;
        }
        out_status->used_bytes += block->used;
        out_status->record_count += block->record_count;
        out_status->partial_records += block->partial_count;
        if (out_status->oldest_id == 0u ||
            block->first_id < out_status->oldest_id) {
            out_status->oldest_id = block->first_id;
        }
        if (block->last_id > out_status->newest_id) {
            out_status->newest_id = block->last_id;
        }
    }
    if (history->retain_oldest_id != 0u &&
        history->retain_oldest_id > out_status->oldest_id &&
        (out_status->newest_id == 0u ||
         history->retain_oldest_id <= out_status->newest_id)) {
        out_status->oldest_id = history->retain_oldest_id;
    }
}

bool runtime_history_has_active_record(const runtime_history *history) {
    return history != NULL && history->has_active_record != 0u;
}

bool runtime_history_begin_record(
    runtime_history *history,
    const runtime_history_begin *begin) {
    runtime_history_block *block;
    uint8_t *bytes;
    uint64_t delta;

    if (history == NULL || begin == NULL || !history->available ||
        !history->recording || history->has_active_record ||
        begin->kind == RUNTIME_HISTORY_RECORD_MARKER) {
        return false;
    }
    if (history->has_current_block) {
        block = &history->blocks[history->current_block];
        delta = begin->machine_cycle >= block->base_cycle ?
            begin->machine_cycle - block->base_cycle : UINT64_MAX;
        if (block->epoch != history->epoch ||
            block->timeline != history->timeline ||
            delta > UINT32_MAX ||
            HISTORY_MAX_RECORD_SIZE > history->block_size - block->used) {
            block = NULL;
        }
    } else {
        block = NULL;
    }
    if (block == NULL) {
        if (!history_ensure_space(
                history,
                begin->machine_cycle,
                HISTORY_MAX_RECORD_SIZE)) {
            return false;
        }
        block = &history->blocks[history->current_block];
        delta = begin->machine_cycle - block->base_cycle;
    }
    bytes = history_block_bytes(history, history->current_block) + block->used;
    memset(bytes, 0, HISTORY_EXEC_HEADER_SIZE);
    history_write_u32(bytes + 0, (uint32_t)delta);
    history_write_u16(bytes + 4, begin->pc);
    memcpy(bytes + 6, &begin->a, 5u);
    history_write_u16(bytes + 14, UINT16_MAX);
    history_write_u16(bytes + 16, UINT16_MAX);
    history_write_u16(bytes + 18, UINT16_MAX);
    bytes[21] = (uint8_t)begin->kind | HISTORY_TAG_PARTIAL;

    history->active_offset = block->used;
    history->active_block = block;
    history->active_header = bytes;
    history->active_access_cursor = bytes + HISTORY_EXEC_HEADER_SIZE;
    history->active_id = history->next_id++;
    history->active_start_cycle = begin->machine_cycle;
    history->has_active_record = 1u;
    block->used += HISTORY_EXEC_HEADER_SIZE;
    block->record_count++;
    block->partial_count++;
    if (block->record_count == 1u) {
        block->first_id = history->active_id;
    }
    block->last_id = history->active_id;
    return true;
}

bool runtime_history_append_observed_access(
    runtime_history *history,
    c6510_bus_access_kind kind,
    uint16_t address,
    uint8_t value,
    uint64_t machine_cycle) {
    runtime_history_block *block;
    uint8_t *header;
    uint8_t tag;
    uint64_t offset64;
    uint16_t offset;
    runtime_history_record_kind record_kind;

    if (!history->has_active_record) {
        return false;
    }
    header = history->active_header;
    tag = header[21];
    record_kind =
        (runtime_history_record_kind)(tag & HISTORY_TAG_KIND_MASK);
    if (machine_cycle < history->active_start_cycle) {
        offset64 = 0u;
        header[21] |= HISTORY_TAG_TIMING_TRUNCATED;
    } else {
        offset64 = machine_cycle - history->active_start_cycle;
    }
    if (offset64 > UINT16_MAX) {
        offset = UINT16_MAX;
        header[21] |= HISTORY_TAG_TIMING_TRUNCATED;
    } else {
        offset = (uint16_t)offset64;
    }

    if (record_kind == RUNTIME_HISTORY_RECORD_INSTRUCTION &&
        kind == C6510_BUS_ACCESS_OPCODE_FETCH) {
        header[11] = value;
        history_write_u16(header + 14, offset);
        header[21] =
            (uint8_t)((header[21] & ~HISTORY_TAG_LENGTH_MASK) |
                      (1u << HISTORY_TAG_LENGTH_SHIFT));
        return true;
    }
    if (record_kind == RUNTIME_HISTORY_RECORD_INSTRUCTION &&
        kind == C6510_BUS_ACCESS_OPERAND_READ) {
        uint8_t length =
            (uint8_t)((header[21] & HISTORY_TAG_LENGTH_MASK) >>
                      HISTORY_TAG_LENGTH_SHIFT);
        if (length <= 1u) {
            header[12] = value;
            history_write_u16(header + 16, offset);
            length = 2u;
        } else if (length == 2u) {
            header[13] = value;
            history_write_u16(header + 18, offset);
            length = 3u;
        } else {
            return true;
        }
        header[21] =
            (uint8_t)((header[21] & ~HISTORY_TAG_LENGTH_MASK) |
                      (length << HISTORY_TAG_LENGTH_SHIFT));
        return true;
    }

    if (header[20] >= RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD) {
        if ((header[21] & HISTORY_TAG_ACCESS_TRUNCATED) == 0u) {
            header[21] |= HISTORY_TAG_ACCESS_TRUNCATED;
            history->truncated_accesses++;
        }
        return true;
    }
    block = history->active_block;
    {
        uint8_t *access = history->active_access_cursor;
        history_write_u16(access + 0, address);
        history_write_u16(access + 2, offset);
        access[4] = value;
        access[5] = (uint8_t)kind;
        history->active_access_cursor += HISTORY_ACCESS_SIZE;
        block->used += HISTORY_ACCESS_SIZE;
        header[20]++;
    }
    return true;
}

bool runtime_history_append_access(
    runtime_history *history,
    c6510_bus_access_kind kind,
    uint16_t address,
    uint8_t value,
    uint64_t machine_cycle) {
    if (history == NULL || !history->available || !history->recording) {
        return false;
    }
    return runtime_history_append_observed_access(
        history, kind, address, value, machine_cycle);
}

bool runtime_history_complete_record(runtime_history *history) {
    if (history == NULL || !history->has_active_record) {
        return false;
    }
    if ((history->active_header[21] & HISTORY_TAG_PARTIAL) != 0u &&
        history->active_block != NULL &&
        history->active_block->partial_count > 0u) {
        history->active_block->partial_count--;
    }
    history->active_header[21] &= (uint8_t)~HISTORY_TAG_PARTIAL;
    history->has_active_record = 0u;
    return true;
}

bool runtime_history_seal_partial(runtime_history *history) {
    if (history == NULL) {
        return false;
    }
    if (!history->has_active_record) {
        return true;
    }
    history->has_active_record = 0u;
    history->active_block = NULL;
    history->active_header = NULL;
    history->active_access_cursor = NULL;
    return true;
}

bool runtime_history_append_marker(
    runtime_history *history,
    uint16_t marker_kind,
    uint32_t arg0,
    uint32_t arg1,
    uint64_t machine_cycle) {
    runtime_history_block *block;
    uint8_t *bytes;
    uint64_t id;
    uint64_t delta;

    if (history == NULL || !history->available || !history->recording ||
        history->has_active_record ||
        !history_ensure_space(history, machine_cycle, HISTORY_EXEC_HEADER_SIZE)) {
        return false;
    }
    block = &history->blocks[history->current_block];
    bytes = history_block_bytes(history, history->current_block) + block->used;
    memset(bytes, 0, HISTORY_EXEC_HEADER_SIZE);
    delta = machine_cycle - block->base_cycle;
    history_write_u32(bytes + 0, (uint32_t)delta);
    history_write_u16(bytes + 4, marker_kind);
    history_write_u32(bytes + 6, arg0);
    history_write_u32(bytes + 10, arg1);
    bytes[21] = RUNTIME_HISTORY_RECORD_MARKER;
    id = history->next_id++;
    block->used += HISTORY_EXEC_HEADER_SIZE;
    block->record_count++;
    if (block->record_count == 1u) {
        block->first_id = id;
    }
    block->last_id = id;
    return true;
}

bool runtime_history_stop(runtime_history *history, uint64_t machine_cycle) {
    if (history == NULL || !history->available) {
        return false;
    }
    if (!history->recording) {
        return true;
    }
    (void)runtime_history_seal_partial(history);
    if (!runtime_history_append_marker(
            history,
            RUNTIME_HISTORY_MARKER_RECORDER_STOP,
            0u,
            0u,
            machine_cycle)) {
        return false;
    }
    history->recording = 0u;
    return true;
}

bool runtime_history_resume(runtime_history *history, uint64_t machine_cycle) {
    if (history == NULL || !history->available) {
        return false;
    }
    if (history->recording) {
        return true;
    }
    history->recording = 1u;
    if (!runtime_history_append_marker(
            history,
            RUNTIME_HISTORY_MARKER_RECORDER_RESUME,
            0u,
            0u,
            machine_cycle)) {
        history->recording = 0u;
        return false;
    }
    return true;
}

static bool history_clear_epoch(
    runtime_history *history,
    uint64_t machine_cycle,
    uint32_t timeline,
    uint16_t first_marker_kind) {
    bool was_recording;

    if (history == NULL || !history->available) {
        return false;
    }
    was_recording = history->recording != 0u;
    memset(
        history->blocks,
        0,
        history->block_count * sizeof(runtime_history_block));
    history->has_current_block = 0u;
    history->has_active_record = 0u;
    history->active_block = NULL;
    history->active_header = NULL;
    history->active_access_cursor = NULL;
    history->epoch++;
    if (history->epoch == 0u) {
        history->epoch = 1u;
    }
    history->next_id = 1u;
    history->retain_oldest_id = 0u;
    history->timeline = timeline;
    history->wrap_count = 0u;
    history->truncated_accesses = 0u;
    history->recording = was_recording ? 1u : 0u;
    if (was_recording) {
        return runtime_history_append_marker(
            history,
            first_marker_kind,
            0u,
            0u,
            machine_cycle);
    }
    return true;
}

bool runtime_history_clear(runtime_history *history, uint64_t machine_cycle) {
    return history_clear_epoch(
        history,
        machine_cycle,
        0u,
        RUNTIME_HISTORY_MARKER_RECORDER_START);
}

bool runtime_history_clear_for_state_load(
    runtime_history *history,
    uint64_t machine_cycle) {
    return history_clear_epoch(
        history,
        machine_cycle,
        1u,
        RUNTIME_HISTORY_MARKER_STATE_LOAD);
}

bool runtime_history_transition_timeline(runtime_history *history) {
    if (history == NULL || !history->available) {
        return false;
    }
    (void)runtime_history_seal_partial(history);
    history_seal_current_block(history);
    history->timeline++;
    return true;
}

bool runtime_history_retain_from(
    runtime_history *history,
    uint64_t epoch,
    uint64_t id)
{
    runtime_history_record rec;
    size_t i;

    if (history == NULL || !history->available || id == 0u) {
        return false;
    }
    if (epoch != history->epoch) {
        return false;
    }
    if (!runtime_history_lookup(history, epoch, id, &rec)) {
        return false;
    }
    history->retain_oldest_id = id;
    for (i = 0u; i < history->block_count; ++i) {
        runtime_history_block *block = &history->blocks[i];
        if (!block->occupied || block->epoch != history->epoch) {
            continue;
        }
        if (block->last_id < id) {
            memset(block, 0, sizeof(*block));
        }
    }
    return true;
}

bool runtime_history_set_timeline(runtime_history *history, uint32_t timeline) {
    if (history == NULL || !history->available) {
        return false;
    }
    (void)runtime_history_seal_partial(history);
    history_seal_current_block(history);
    history->timeline = timeline;
    return true;
}

bool runtime_history_lookup(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record) {
    size_t block_index;
    size_t offset;

    return history_find_record(
               history, epoch, id, &block_index, &offset) &&
        history_decode_at(
            history, block_index, offset, id, out_record, NULL);
}

bool runtime_history_first(
    const runtime_history *history,
    runtime_history_record *out_record) {
    runtime_history_status status;
    runtime_history_get_status(history, &status);
    return status.oldest_id != 0u &&
        runtime_history_lookup(
            history, status.epoch, status.oldest_id, out_record);
}

bool runtime_history_last(
    const runtime_history *history,
    runtime_history_record *out_record) {
    runtime_history_status status;
    runtime_history_get_status(history, &status);
    return status.newest_id != 0u &&
        runtime_history_lookup(
            history, status.epoch, status.newest_id, out_record);
}

bool runtime_history_next(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record) {
    return id != UINT64_MAX &&
        runtime_history_lookup(history, epoch, id + 1u, out_record);
}

bool runtime_history_previous(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record) {
    return id > 1u &&
        runtime_history_lookup(history, epoch, id - 1u, out_record);
}

static void history_retained_bounds(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t *out_oldest,
    uint64_t *out_newest) {
    size_t i;
    uint64_t oldest = 0u;
    uint64_t newest = 0u;

    if (history != NULL) {
        for (i = 0u; i < history->block_count; ++i) {
            const runtime_history_block *block = &history->blocks[i];
            if (!block->occupied || block->epoch != epoch ||
                block->record_count == 0u) {
                continue;
            }
            if (oldest == 0u || block->first_id < oldest) {
                oldest = block->first_id;
            }
            if (block->last_id > newest) {
                newest = block->last_id;
            }
        }
    }
    if (out_oldest != NULL) {
        *out_oldest = oldest;
    }
    if (out_newest != NULL) {
        *out_newest = newest;
    }
}

static bool history_query_is_valid(const runtime_history_query *query) {
    size_t i;
    uint16_t valid_access_mask =
        RUNTIME_HISTORY_ACCESS_PHYSICAL_MASK |
        RUNTIME_HISTORY_ACCESS_EXECUTE;

    if (query == NULL ||
        (query->direction != RUNTIME_HISTORY_QUERY_BACKWARD &&
         query->direction != RUNTIME_HISTORY_QUERY_FORWARD) ||
        (query->has_pc && query->pc_last < query->pc_first) ||
        (query->has_address &&
         query->address_last < query->address_first) ||
        (query->has_cycle &&
         query->cycle_last < query->cycle_first) ||
        (query->has_access &&
         (query->access_mask == 0u ||
          (query->access_mask & (uint16_t)~valid_access_mask) != 0u)) ||
        query->opcode_pattern_length > RUNTIME_HISTORY_MAX_OPCODE_PATTERN) {
        return false;
    }
    for (i = 0u; i < query->opcode_pattern_length; ++i) {
        if ((query->opcode_pattern[i].value &
             (uint8_t)~query->opcode_pattern[i].mask) != 0u) {
            return false;
        }
    }
    return true;
}

static bool history_record_matches_access(
    const runtime_history_record *record,
    const runtime_history_query *query) {
    uint16_t mask;
    size_t i;

    if (!query->has_address && !query->has_access && !query->has_value) {
        return true;
    }
    mask = query->has_access ?
        query->access_mask : RUNTIME_HISTORY_ACCESS_PHYSICAL_MASK;

    if ((mask & RUNTIME_HISTORY_ACCESS_EXECUTE) != 0u &&
        record->kind == RUNTIME_HISTORY_RECORD_INSTRUCTION &&
        (!query->has_address ||
         (record->pc >= query->address_first &&
          record->pc <= query->address_last)) &&
        (!query->has_value ||
         (record->opcode & query->value_mask) ==
             (query->value & query->value_mask))) {
        return true;
    }
    for (i = 0u; i < record->access_count; ++i) {
        const runtime_history_access *access = &record->accesses[i];
        uint16_t access_bit;

        if ((unsigned)access->kind >
            (unsigned)C6510_BUS_ACCESS_VECTOR_READ) {
            continue;
        }
        access_bit = (uint16_t)(1u << access->kind);
        if ((mask & access_bit) == 0u ||
            (query->has_address &&
             (access->address < query->address_first ||
              access->address > query->address_last)) ||
            (query->has_value &&
             (access->value & query->value_mask) !=
                 (query->value & query->value_mask))) {
            continue;
        }
        return true;
    }
    return false;
}

static bool history_next_record_location(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    size_t block_index,
    size_t offset,
    size_t size,
    size_t *out_block_index,
    size_t *out_offset) {
    const runtime_history_block *block = &history->blocks[block_index];
    size_t next_offset = offset + size;
    size_t i;

    if (next_offset < block->used) {
        *out_block_index = block_index;
        *out_offset = next_offset;
        return true;
    }
    if (id == UINT64_MAX) {
        return false;
    }
    for (i = 1u; i <= history->block_count; ++i) {
        size_t next_block = (block_index + i) % history->block_count;
        const runtime_history_block *candidate =
            &history->blocks[next_block];
        if (!candidate->occupied || candidate->epoch != epoch ||
            candidate->record_count == 0u) {
            continue;
        }
        if (candidate->first_id != id + 1u) {
            return false;
        }
        *out_block_index = next_block;
        *out_offset = 0u;
        return true;
    }
    return false;
}

static bool history_record_matches_opcode_pattern(
    const runtime_history *history,
    const runtime_history_query *query,
    const runtime_history_record *anchor,
    size_t block_index,
    size_t offset,
    size_t size) {
    runtime_history_record record = *anchor;
    size_t current_block = block_index;
    size_t current_offset = offset;
    size_t current_size = size;
    size_t i;

    for (i = 0u; i < query->opcode_pattern_length; ++i) {
        const runtime_history_opcode_pattern_byte *pattern =
            &query->opcode_pattern[i];
        if (record.kind != RUNTIME_HISTORY_RECORD_INSTRUCTION ||
            record.timeline != anchor->timeline ||
            (record.opcode & pattern->mask) != pattern->value) {
            return false;
        }
        if (i + 1u < query->opcode_pattern_length) {
            if (!history_next_record_location(
                    history,
                    anchor->epoch,
                    record.id,
                    current_block,
                    current_offset,
                    current_size,
                    &current_block,
                    &current_offset) ||
                !history_decode_at(
                    history,
                    current_block,
                    current_offset,
                    record.id + 1u,
                    &record,
                    &current_size)) {
                return false;
            }
        }
    }
    return true;
}

static bool history_record_matches_query(
    const runtime_history *history,
    const runtime_history_query *query,
    const runtime_history_record *record,
    size_t block_index,
    size_t offset,
    size_t size) {
    if ((query->has_timeline && record->timeline != query->timeline) ||
        (query->has_cycle &&
         (record->machine_cycle < query->cycle_first ||
          record->machine_cycle > query->cycle_last)) ||
        (query->has_pc &&
         (record->kind == RUNTIME_HISTORY_RECORD_MARKER ||
          record->pc < query->pc_first ||
          record->pc > query->pc_last)) ||
        !history_record_matches_access(record, query)) {
        return false;
    }
    return query->opcode_pattern_length == 0u ||
        history_record_matches_opcode_pattern(
            history, query, record, block_index, offset, size);
}

runtime_history_query_result runtime_history_find(
    const runtime_history *history,
    const runtime_history_query *query,
    uint64_t from_id,
    size_t limit,
    runtime_history_record *out_records,
    runtime_history_page *out_page,
    runtime_history_query_stats *out_stats) {
    uint64_t epoch;
    uint64_t oldest;
    uint64_t newest;
    uint64_t scan_id;
    size_t block_index;
    size_t start_offset;
    size_t *offsets = NULL;
    size_t visited;
    runtime_history_query_result result = RUNTIME_HISTORY_QUERY_FAILED;

    if (out_page == NULL || out_records == NULL || limit == 0u ||
        limit > RUNTIME_HISTORY_MAX_QUERY_RECORDS ||
        !history_query_is_valid(query)) {
        return RUNTIME_HISTORY_QUERY_INVALID;
    }
    memset(out_page, 0, sizeof(*out_page));
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
    }
    if (history == NULL || !history->available) {
        return RUNTIME_HISTORY_QUERY_UNAVAILABLE;
    }
    epoch = query->has_epoch ? query->epoch : history->epoch;
    if (epoch != history->epoch) {
        return RUNTIME_HISTORY_QUERY_EPOCH_MISMATCH;
    }
    history_retained_bounds(history, epoch, &oldest, &newest);
    if (oldest == 0u) {
        return RUNTIME_HISTORY_QUERY_OK;
    }
    scan_id = from_id != 0u ? from_id :
        (query->direction == RUNTIME_HISTORY_QUERY_FORWARD ? oldest : newest);
    if (scan_id < oldest || scan_id > newest ||
        !history_find_record(
            history, epoch, scan_id, &block_index, &start_offset)) {
        return RUNTIME_HISTORY_QUERY_RECORD_NOT_RETAINED;
    }
    offsets = (size_t *)malloc(
        (history->block_size / HISTORY_EXEC_HEADER_SIZE + 1u) *
        sizeof(*offsets));
    if (offsets == NULL) {
        return RUNTIME_HISTORY_QUERY_FAILED;
    }

    for (visited = 0u; visited < history->block_count; ++visited) {
        const runtime_history_block *block = &history->blocks[block_index];
        size_t offset = 0u;
        size_t record_count = 0u;
        size_t record_index;

        if (!block->occupied || block->epoch != epoch ||
            block->record_count == 0u ||
            scan_id < block->first_id || scan_id > block->last_id) {
            result = RUNTIME_HISTORY_QUERY_FAILED;
            goto done;
        }
        if (out_stats != NULL) {
            out_stats->blocks_visited++;
        }
        while (record_count < block->record_count && offset < block->used) {
            size_t record_size = history_record_size_from_bytes(
                history_block_bytes(history, block_index) + offset,
                block->used - offset);
            if (record_size == 0u) {
                result = RUNTIME_HISTORY_QUERY_FAILED;
                goto done;
            }
            offsets[record_count++] = offset;
            offset += record_size;
        }
        if (record_count != block->record_count || offset != block->used) {
            result = RUNTIME_HISTORY_QUERY_FAILED;
            goto done;
        }

        record_index = (size_t)(scan_id - block->first_id);
        for (;;) {
            uint64_t id = block->first_id + record_index;
            runtime_history_record record;
            size_t record_size;

            if (!query->has_timeline ||
                block->timeline == query->timeline) {
                if (!history_decode_at(
                        history,
                        block_index,
                        offsets[record_index],
                        id,
                        &record,
                        &record_size)) {
                    result = RUNTIME_HISTORY_QUERY_FAILED;
                    goto done;
                }
                if (out_stats != NULL) {
                    out_stats->records_decoded++;
                    out_stats->bytes_scanned += record_size;
                }
                if (history_record_matches_query(
                        history,
                        query,
                        &record,
                        block_index,
                        offsets[record_index],
                        record_size)) {
                    out_records[out_page->count++] = record;
                    if (out_page->count == limit) {
                        if (query->direction ==
                            RUNTIME_HISTORY_QUERY_FORWARD) {
                            out_page->next_id =
                                id < newest ? id + 1u : 0u;
                        } else {
                            out_page->next_id =
                                id > oldest ? id - 1u : 0u;
                        }
                        out_page->more = out_page->next_id != 0u;
                        result = RUNTIME_HISTORY_QUERY_OK;
                        goto done;
                    }
                }
            }
            if (query->direction == RUNTIME_HISTORY_QUERY_FORWARD) {
                if (++record_index >= record_count) {
                    break;
                }
            } else {
                if (record_index == 0u) {
                    break;
                }
                record_index--;
            }
        }

        if (query->direction == RUNTIME_HISTORY_QUERY_FORWARD) {
            if (block->last_id >= newest) {
                result = RUNTIME_HISTORY_QUERY_OK;
                goto done;
            }
            scan_id = block->last_id + 1u;
            block_index = (block_index + 1u) % history->block_count;
        } else {
            if (block->first_id <= oldest) {
                result = RUNTIME_HISTORY_QUERY_OK;
                goto done;
            }
            scan_id = block->first_id - 1u;
            block_index =
                (block_index + history->block_count - 1u) %
                history->block_count;
        }
    }
    result = RUNTIME_HISTORY_QUERY_FAILED;

done:
    free(offsets);
    return result;
}

runtime_history_query_result runtime_history_read(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t anchor_id,
    size_t before,
    size_t after,
    runtime_history_record *out_records,
    size_t out_capacity,
    runtime_history_page *out_page) {
    uint64_t oldest;
    uint64_t newest;
    uint64_t first;
    uint64_t last;
    uint64_t id;
    bool clipped_before;
    bool clipped_after;

    if (out_records == NULL || out_page == NULL ||
        before > RUNTIME_HISTORY_MAX_QUERY_RECORDS ||
        after > RUNTIME_HISTORY_MAX_QUERY_RECORDS ||
        out_capacity == 0u) {
        return RUNTIME_HISTORY_QUERY_INVALID;
    }
    memset(out_page, 0, sizeof(*out_page));
    if (history == NULL || !history->available) {
        return RUNTIME_HISTORY_QUERY_UNAVAILABLE;
    }
    if (epoch != history->epoch) {
        return RUNTIME_HISTORY_QUERY_EPOCH_MISMATCH;
    }
    history_retained_bounds(history, epoch, &oldest, &newest);
    if (anchor_id < oldest || anchor_id > newest ||
        !history_find_record(history, epoch, anchor_id, NULL, NULL)) {
        return RUNTIME_HISTORY_QUERY_RECORD_NOT_RETAINED;
    }
    clipped_before = anchor_id - oldest < before;
    clipped_after = newest - anchor_id < after;
    first = clipped_before ? oldest : anchor_id - before;
    last = clipped_after ? newest : anchor_id + after;
    out_page->more = clipped_before || clipped_after;
    for (id = first; id <= last && out_page->count < out_capacity; ++id) {
        if (!runtime_history_lookup(
                history, epoch, id, &out_records[out_page->count])) {
            return RUNTIME_HISTORY_QUERY_FAILED;
        }
        out_page->count++;
        if (id == UINT64_MAX) {
            break;
        }
    }
    if (id <= last) {
        out_page->more = true;
    }
    return RUNTIME_HISTORY_QUERY_OK;
}

bool runtime_history_test_corrupt_record_size(
    runtime_history *history,
    uint64_t id,
    uint8_t access_count) {
    size_t block_index;
    size_t offset;

    if (!history_find_record(
            history, history != NULL ? history->epoch : 0u, id,
            &block_index, &offset)) {
        return false;
    }
    history_block_bytes(history, block_index)[offset + 20u] = access_count;
    return true;
}

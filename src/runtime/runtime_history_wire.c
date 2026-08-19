#include "runtime_history_wire.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void wire_write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void wire_write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void wire_write_u64(uint8_t *p, uint64_t value) {
    wire_write_u32(p, (uint32_t)value);
    wire_write_u32(p + 4, (uint32_t)(value >> 32));
}

runtime_history_wire_result runtime_history_wire_encode(
    uint64_t epoch,
    const runtime_history_record *records,
    size_t record_count,
    bool all_anchor_matches,
    uint64_t anchor_id,
    uint8_t **out_bytes,
    uint32_t *out_length,
    size_t *out_encoded_count,
    bool *out_clipped) {
    size_t encoded_count = 0u;
    size_t total_size = RUNTIME_HISTORY_WIRE_HEADER_SIZE;
    size_t i;
    uint8_t *bytes;
    uint8_t *cursor;

    if (out_bytes == NULL || out_length == NULL ||
        out_encoded_count == NULL || out_clipped == NULL ||
        (record_count != 0u && records == NULL) ||
        record_count > RUNTIME_HISTORY_MAX_CONTEXT_RECORDS) {
        return RUNTIME_HISTORY_WIRE_INVALID;
    }
    *out_bytes = NULL;
    *out_length = 0u;
    *out_encoded_count = 0u;
    *out_clipped = false;

    for (i = 0u; i < record_count; ++i) {
        size_t record_size;
        if (records[i].epoch != epoch ||
            records[i].access_count >
                RUNTIME_HISTORY_MAX_MATERIALIZED_ACCESSES) {
            return RUNTIME_HISTORY_WIRE_INVALID;
        }
        record_size = RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE +
            (size_t)records[i].access_count *
                RUNTIME_HISTORY_WIRE_ACCESS_SIZE;
        if (record_size > UINT16_MAX ||
            total_size > RUNTIME_HISTORY_WIRE_MAX_PAYLOAD - record_size) {
            *out_clipped = true;
            break;
        }
        total_size += record_size;
        encoded_count++;
    }
    bytes = (uint8_t *)malloc(total_size);
    if (bytes == NULL) {
        return RUNTIME_HISTORY_WIRE_ALLOCATION_FAILED;
    }
    memset(bytes, 0, total_size);
    memcpy(bytes, "HST1", 4u);
    wire_write_u16(bytes + 4, 1u);
    wire_write_u64(bytes + 8, epoch);
    wire_write_u32(bytes + 16, (uint32_t)encoded_count);
    cursor = bytes + RUNTIME_HISTORY_WIRE_HEADER_SIZE;

    for (i = 0u; i < encoded_count; ++i) {
        const runtime_history_record *record = &records[i];
        size_t record_size = RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE +
            (size_t)record->access_count *
                RUNTIME_HISTORY_WIRE_ACCESS_SIZE;
        uint8_t flags = 0u;
        size_t access_index;

        wire_write_u16(cursor + 0, (uint16_t)record_size);
        cursor[2] = (uint8_t)record->kind;
        if (record->partial) {
            flags |= 1u << 0;
        }
        if (record->access_truncated) {
            flags |= 1u << 1;
        }
        if (all_anchor_matches || record->id == anchor_id) {
            flags |= 1u << 2;
        }
        if (record->timing_truncated) {
            flags |= 1u << 3;
        }
        cursor[3] = flags;
        wire_write_u32(cursor + 4, record->timeline);
        wire_write_u64(cursor + 8, record->id);
        wire_write_u64(cursor + 16, record->machine_cycle);
        wire_write_u16(cursor + 24, record->pc);
        cursor[26] = record->a;
        cursor[27] = record->x;
        cursor[28] = record->y;
        cursor[29] = record->sp;
        cursor[30] = record->p;
        cursor[31] = record->opcode;
        cursor[32] = record->operand1;
        cursor[33] = record->operand2;
        cursor[34] = record->instruction_length;
        cursor[35] = record->access_count;
        wire_write_u16(cursor + 36, record->marker_kind);
        wire_write_u32(cursor + 40, record->marker_arg0);
        wire_write_u32(cursor + 44, record->marker_arg1);
        for (access_index = 0u;
             access_index < record->access_count;
             ++access_index) {
            const runtime_history_access *access =
                &record->accesses[access_index];
            uint8_t *wire_access =
                cursor + RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE +
                access_index * RUNTIME_HISTORY_WIRE_ACCESS_SIZE;
            wire_write_u16(wire_access + 0, access->address);
            wire_write_u16(wire_access + 2, access->cycle_offset);
            wire_access[4] = access->value;
            wire_access[5] = (uint8_t)access->kind;
        }
        cursor += record_size;
    }

    *out_bytes = bytes;
    *out_length = (uint32_t)total_size;
    *out_encoded_count = encoded_count;
    return RUNTIME_HISTORY_WIRE_OK;
}

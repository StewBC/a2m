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

static uint16_t wire_read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t wire_read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t wire_read_u64(const uint8_t *p) {
    return (uint64_t)wire_read_u32(p) |
        ((uint64_t)wire_read_u32(p + 4) << 32);
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

runtime_history_wire_result runtime_history_wire_decode(
    const uint8_t *bytes,
    size_t length,
    uint64_t *out_epoch,
    runtime_history_record **out_records,
    bool **out_anchor_matches,
    size_t *out_count)
{
    uint16_t version;
    uint16_t header_flags;
    uint64_t epoch;
    uint32_t count;
    uint32_t reserved;
    size_t offset;
    size_t i;
    runtime_history_record *records = NULL;
    bool *anchors = NULL;

    if (out_epoch != NULL) {
        *out_epoch = 0u;
    }
    if (out_records != NULL) {
        *out_records = NULL;
    }
    if (out_anchor_matches != NULL) {
        *out_anchor_matches = NULL;
    }
    if (out_count != NULL) {
        *out_count = 0u;
    }

    if (bytes == NULL || out_epoch == NULL || out_records == NULL ||
        out_count == NULL || length < RUNTIME_HISTORY_WIRE_HEADER_SIZE ||
        length > RUNTIME_HISTORY_WIRE_MAX_PAYLOAD ||
        memcmp(bytes, "HST1", 4u) != 0) {
        return RUNTIME_HISTORY_WIRE_INVALID;
    }

    version = wire_read_u16(bytes + 4);
    header_flags = wire_read_u16(bytes + 6);
    epoch = wire_read_u64(bytes + 8);
    count = wire_read_u32(bytes + 16);
    reserved = wire_read_u32(bytes + 20);
    if (version != 1u || header_flags != 0u || reserved != 0u ||
        count > RUNTIME_HISTORY_MAX_CONTEXT_RECORDS) {
        return RUNTIME_HISTORY_WIRE_INVALID;
    }

    if (count > 0u) {
        records = (runtime_history_record *)calloc(
            count, sizeof(runtime_history_record));
        if (records == NULL) {
            return RUNTIME_HISTORY_WIRE_ALLOCATION_FAILED;
        }
        if (out_anchor_matches != NULL) {
            anchors = (bool *)calloc(count, sizeof(bool));
            if (anchors == NULL) {
                free(records);
                return RUNTIME_HISTORY_WIRE_ALLOCATION_FAILED;
            }
        }
    }

    offset = RUNTIME_HISTORY_WIRE_HEADER_SIZE;
    for (i = 0u; i < (size_t)count; ++i) {
        runtime_history_record *record;
        uint16_t record_size;
        uint8_t kind;
        uint8_t record_flags;
        uint8_t access_count;
        uint16_t expected_size;
        uint16_t record_reserved;
        size_t access_index;

        if (offset + RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE > length) {
            free(records);
            free(anchors);
            return RUNTIME_HISTORY_WIRE_INVALID;
        }
        record_size = wire_read_u16(bytes + offset);
        kind = bytes[offset + 2];
        record_flags = bytes[offset + 3];
        access_count = bytes[offset + 35];
        expected_size = (uint16_t)(
            RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE +
            (size_t)access_count * RUNTIME_HISTORY_WIRE_ACCESS_SIZE);
        record_reserved = wire_read_u16(bytes + offset + 38);
        if (kind > (uint8_t)RUNTIME_HISTORY_RECORD_MARKER ||
            record_size != expected_size ||
            offset + (size_t)record_size > length ||
            access_count > RUNTIME_HISTORY_MAX_MATERIALIZED_ACCESSES ||
            record_reserved != 0u) {
            free(records);
            free(anchors);
            return RUNTIME_HISTORY_WIRE_INVALID;
        }

        record = &records[i];
        record->epoch = epoch;
        record->kind = (runtime_history_record_kind)kind;
        record->partial = (record_flags & (1u << 0)) != 0u;
        record->access_truncated = (record_flags & (1u << 1)) != 0u;
        record->timing_truncated = (record_flags & (1u << 3)) != 0u;
        if (anchors != NULL) {
            anchors[i] = (record_flags & (1u << 2)) != 0u;
        }
        record->timeline = wire_read_u32(bytes + offset + 4);
        record->id = wire_read_u64(bytes + offset + 8);
        record->machine_cycle = wire_read_u64(bytes + offset + 16);
        record->pc = wire_read_u16(bytes + offset + 24);
        record->a = bytes[offset + 26];
        record->x = bytes[offset + 27];
        record->y = bytes[offset + 28];
        record->sp = bytes[offset + 29];
        record->p = bytes[offset + 30];
        record->opcode = bytes[offset + 31];
        record->operand1 = bytes[offset + 32];
        record->operand2 = bytes[offset + 33];
        record->instruction_length = bytes[offset + 34];
        record->access_count = access_count;
        record->marker_kind = wire_read_u16(bytes + offset + 36);
        record->marker_arg0 = wire_read_u32(bytes + offset + 40);
        record->marker_arg1 = wire_read_u32(bytes + offset + 44);

        for (access_index = 0u; access_index < access_count; ++access_index) {
            const uint8_t *wire_access =
                bytes + offset + RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE +
                access_index * RUNTIME_HISTORY_WIRE_ACCESS_SIZE;
            uint8_t access_kind = wire_access[5];
            uint16_t access_reserved = wire_read_u16(wire_access + 6);
            if (access_kind > (uint8_t)C6510_BUS_ACCESS_VECTOR_READ ||
                access_reserved != 0u) {
                free(records);
                free(anchors);
                return RUNTIME_HISTORY_WIRE_INVALID;
            }
            record->accesses[access_index].address =
                wire_read_u16(wire_access + 0);
            record->accesses[access_index].cycle_offset =
                wire_read_u16(wire_access + 2);
            record->accesses[access_index].value = wire_access[4];
            record->accesses[access_index].kind =
                (c6510_bus_access_kind)access_kind;
        }
        offset += record_size;
    }

    if (offset != length) {
        free(records);
        free(anchors);
        return RUNTIME_HISTORY_WIRE_INVALID;
    }

    *out_epoch = epoch;
    *out_records = records;
    *out_count = (size_t)count;
    if (out_anchor_matches != NULL) {
        *out_anchor_matches = anchors;
    }
    return RUNTIME_HISTORY_WIRE_OK;
}

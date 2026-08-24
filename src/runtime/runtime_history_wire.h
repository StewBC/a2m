#pragma once

#include "runtime_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RUNTIME_HISTORY_WIRE_HEADER_SIZE = 24,
    RUNTIME_HISTORY_WIRE_RECORD_HEADER_SIZE = 48,
    RUNTIME_HISTORY_WIRE_ACCESS_SIZE = 8,
    RUNTIME_HISTORY_WIRE_MAX_PAYLOAD = 1024 * 1024
};

typedef enum runtime_history_wire_result {
    RUNTIME_HISTORY_WIRE_OK = 0,
    RUNTIME_HISTORY_WIRE_INVALID,
    RUNTIME_HISTORY_WIRE_ALLOCATION_FAILED
} runtime_history_wire_result;

runtime_history_wire_result runtime_history_wire_encode(
    uint64_t epoch,
    const runtime_history_record *records,
    size_t record_count,
    bool all_anchor_matches,
    uint64_t anchor_id,
    uint8_t **out_bytes,
    uint32_t *out_length,
    size_t *out_encoded_count,
    bool *out_clipped);

/*
 * Decode one HST1 v1 payload (same validation as Ctl.decode_hst1).
 * On OK: *out_records is malloc'd (count elements); caller free()s it.
 * If out_anchor_matches != NULL, also malloc a parallel bool array (free it).
 * On failure: outs are NULL/0 and nothing is allocated.
 */
runtime_history_wire_result runtime_history_wire_decode(
    const uint8_t *bytes,
    size_t length,
    uint64_t *out_epoch,
    runtime_history_record **out_records,
    bool **out_anchor_matches,
    size_t *out_count);

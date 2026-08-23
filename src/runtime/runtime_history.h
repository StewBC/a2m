#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bus access kinds for history records (masks must stay stable for wire). */
typedef enum c6510_bus_access_kind {
    C6510_BUS_ACCESS_DATA_READ = 0,
    C6510_BUS_ACCESS_DATA_WRITE,
    C6510_BUS_ACCESS_OPCODE_FETCH,
    C6510_BUS_ACCESS_OPERAND_READ,
    C6510_BUS_ACCESS_DUMMY_READ,
    C6510_BUS_ACCESS_RMW_DUMMY_WRITE,
    C6510_BUS_ACCESS_STACK_READ,
    C6510_BUS_ACCESS_STACK_WRITE,
    C6510_BUS_ACCESS_VECTOR_READ
} c6510_bus_access_kind;

enum {
    RUNTIME_HISTORY_DEFAULT_MEMORY_MB = 256,
    RUNTIME_HISTORY_DEFAULT_BLOCK_SIZE = 64 * 1024,
    RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD = 64,
    RUNTIME_HISTORY_MAX_MATERIALIZED_ACCESSES =
        RUNTIME_HISTORY_MAX_ACCESSES_PER_RECORD + 3,
    RUNTIME_HISTORY_MAX_OPCODE_PATTERN = 32,
    RUNTIME_HISTORY_MAX_QUERY_RECORDS = 256,
    RUNTIME_HISTORY_MAX_CONTEXT_RECORDS =
        RUNTIME_HISTORY_MAX_QUERY_RECORDS * 2 + 1,
};

typedef enum runtime_history_unavailable_reason {
    RUNTIME_HISTORY_UNAVAILABLE_NONE = 0,
    RUNTIME_HISTORY_UNAVAILABLE_DISABLED_BY_CONFIG,
    RUNTIME_HISTORY_UNAVAILABLE_ALLOCATION_FAILED,
    RUNTIME_HISTORY_UNAVAILABLE_INVALID_CAPACITY
} runtime_history_unavailable_reason;

typedef enum runtime_history_record_kind {
    RUNTIME_HISTORY_RECORD_INSTRUCTION = 0,
    RUNTIME_HISTORY_RECORD_IRQ,
    RUNTIME_HISTORY_RECORD_NMI,
    RUNTIME_HISTORY_RECORD_MARKER
} runtime_history_record_kind;

typedef enum runtime_history_marker_kind {
    RUNTIME_HISTORY_MARKER_RECORDER_START = 1,
    RUNTIME_HISTORY_MARKER_RECORDER_STOP = 2,
    RUNTIME_HISTORY_MARKER_RECORDER_RESUME = 3,
    RUNTIME_HISTORY_MARKER_RESET_COMPLETE = 4,
    RUNTIME_HISTORY_MARKER_STATE_LOAD = 5,
    RUNTIME_HISTORY_MARKER_PROGRAM_INJECT = 6,
    RUNTIME_HISTORY_MARKER_CRT_ATTACH = 7,
    RUNTIME_HISTORY_MARKER_ASSEMBLE = 8,
    RUNTIME_HISTORY_MARKER_DIRECT_MEMORY_WRITE = 9,
    RUNTIME_HISTORY_MARKER_KERNAL_LOAD_TRAP = 10,
    RUNTIME_HISTORY_MARKER_KERNAL_SAVE_TRAP = 11,
    RUNTIME_HISTORY_MARKER_CLOCK_DISCONTINUITY = 12,
    RUNTIME_HISTORY_MARKER_MEDIA_CHANGED = 13
} runtime_history_marker_kind;

typedef enum runtime_history_media_change_kind {
    RUNTIME_HISTORY_MEDIA_CHANGE_UNKNOWN = 0,
    RUNTIME_HISTORY_MEDIA_CHANGE_GUEST_WRITE = 1,
    RUNTIME_HISTORY_MEDIA_CHANGE_HOST_DIRECTORY = 2
} runtime_history_media_change_kind;

typedef enum runtime_history_reset_kind {
    RUNTIME_HISTORY_RESET_UNKNOWN = 0,
    RUNTIME_HISTORY_RESET_INITIAL_STARTUP = 1,
    RUNTIME_HISTORY_RESET_EXPLICIT = 2,
    RUNTIME_HISTORY_RESET_MACHINE_CONFIG = 3,
    RUNTIME_HISTORY_RESET_CRT_ATTACH = 4,
    RUNTIME_HISTORY_RESET_PROGRAM_LOAD = 5,
    RUNTIME_HISTORY_RESET_ASSEMBLE_RESET_FIRST = 6,
    RUNTIME_HISTORY_RESET_BIN_LOAD_RESET_FIRST = 7
} runtime_history_reset_kind;

typedef enum runtime_history_clock_discontinuity_kind {
    RUNTIME_HISTORY_CLOCK_DISCONTINUITY_UNKNOWN = 0,
    RUNTIME_HISTORY_CLOCK_DISCONTINUITY_VIDEO_STANDARD_CHANGE = 1
} runtime_history_clock_discontinuity_kind;

typedef struct runtime_history_access {
    uint16_t address;
    uint16_t cycle_offset;
    uint8_t value;
    c6510_bus_access_kind kind;
} runtime_history_access;

typedef struct runtime_history_begin {
    runtime_history_record_kind kind;
    uint64_t machine_cycle;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
} runtime_history_begin;

typedef struct runtime_history_record {
    uint64_t epoch;
    uint64_t id;
    uint32_t timeline;
    uint64_t machine_cycle;
    runtime_history_record_kind kind;
    uint16_t pc;
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint8_t p;
    uint8_t opcode;
    uint8_t operand1;
    uint8_t operand2;
    uint8_t instruction_length;
    uint8_t access_count;
    uint16_t marker_kind;
    uint32_t marker_arg0;
    uint32_t marker_arg1;
    bool partial;
    bool access_truncated;
    bool timing_truncated;
    runtime_history_access accesses[RUNTIME_HISTORY_MAX_MATERIALIZED_ACCESSES];
} runtime_history_record;

typedef struct runtime_history_status {
    bool available;
    bool recording;
    runtime_history_unavailable_reason unavailable_reason;
    size_t requested_bytes;
    size_t capacity_bytes;
    size_t used_bytes;
    uint64_t epoch;
    uint32_t timeline;
    uint64_t record_count;
    uint64_t oldest_id;
    uint64_t newest_id;
    uint64_t wrap_count;
    uint64_t partial_records;
    uint64_t truncated_accesses;
} runtime_history_status;

typedef enum runtime_history_query_direction {
    RUNTIME_HISTORY_QUERY_BACKWARD = 0,
    RUNTIME_HISTORY_QUERY_FORWARD
} runtime_history_query_direction;

typedef enum runtime_history_from_kind {
    RUNTIME_HISTORY_FROM_DEFAULT = 0,
    RUNTIME_HISTORY_FROM_ID,
    RUNTIME_HISTORY_FROM_OLDEST,
    RUNTIME_HISTORY_FROM_NEWEST
} runtime_history_from_kind;

enum {
    RUNTIME_HISTORY_ACCESS_DATA_READ =
        1u << C6510_BUS_ACCESS_DATA_READ,
    RUNTIME_HISTORY_ACCESS_DATA_WRITE =
        1u << C6510_BUS_ACCESS_DATA_WRITE,
    RUNTIME_HISTORY_ACCESS_OPCODE =
        1u << C6510_BUS_ACCESS_OPCODE_FETCH,
    RUNTIME_HISTORY_ACCESS_OPERAND =
        1u << C6510_BUS_ACCESS_OPERAND_READ,
    RUNTIME_HISTORY_ACCESS_DUMMY_READ =
        1u << C6510_BUS_ACCESS_DUMMY_READ,
    RUNTIME_HISTORY_ACCESS_RMW_DUMMY_WRITE =
        1u << C6510_BUS_ACCESS_RMW_DUMMY_WRITE,
    RUNTIME_HISTORY_ACCESS_STACK_READ =
        1u << C6510_BUS_ACCESS_STACK_READ,
    RUNTIME_HISTORY_ACCESS_STACK_WRITE =
        1u << C6510_BUS_ACCESS_STACK_WRITE,
    RUNTIME_HISTORY_ACCESS_VECTOR_READ =
        1u << C6510_BUS_ACCESS_VECTOR_READ,
    RUNTIME_HISTORY_ACCESS_EXECUTE = 1u << 9,
    RUNTIME_HISTORY_ACCESS_PHYSICAL_MASK =
        (1u << (C6510_BUS_ACCESS_VECTOR_READ + 1u)) - 1u
};

typedef struct runtime_history_opcode_pattern_byte {
    uint8_t value;
    uint8_t mask;
} runtime_history_opcode_pattern_byte;

typedef struct runtime_history_query {
    bool has_epoch;
    uint64_t epoch;
    bool has_timeline;
    uint32_t timeline;
    bool has_cycle;
    uint64_t cycle_first;
    uint64_t cycle_last;
    bool has_pc;
    uint16_t pc_first;
    uint16_t pc_last;
    bool has_address;
    uint16_t address_first;
    uint16_t address_last;
    bool has_access;
    uint16_t access_mask;
    bool has_value;
    uint8_t value;
    uint8_t value_mask;
    uint8_t opcode_pattern_length;
    runtime_history_opcode_pattern_byte
        opcode_pattern[RUNTIME_HISTORY_MAX_OPCODE_PATTERN];
    runtime_history_query_direction direction;
} runtime_history_query;

typedef struct runtime_history_query_stats {
    uint64_t blocks_visited;
    uint64_t bytes_scanned;
    uint64_t records_decoded;
} runtime_history_query_stats;

typedef enum runtime_history_query_result {
    RUNTIME_HISTORY_QUERY_OK = 0,
    RUNTIME_HISTORY_QUERY_UNAVAILABLE,
    RUNTIME_HISTORY_QUERY_EPOCH_MISMATCH,
    RUNTIME_HISTORY_QUERY_RECORD_NOT_RETAINED,
    RUNTIME_HISTORY_QUERY_INVALID,
    RUNTIME_HISTORY_QUERY_FAILED
} runtime_history_query_result;

typedef struct runtime_history_page {
    size_t count;
    uint64_t next_id;
    bool more;
} runtime_history_page;

typedef void *(*runtime_history_alloc_fn)(size_t size, void *user);
typedef void (*runtime_history_free_fn)(void *ptr, void *user);

typedef struct runtime_history_allocator {
    runtime_history_alloc_fn alloc;
    runtime_history_free_fn free;
    void *user;
} runtime_history_allocator;

typedef struct runtime_history runtime_history;

runtime_history *runtime_history_create(size_t requested_bytes);
runtime_history *runtime_history_create_ex(
    size_t requested_bytes,
    size_t block_size,
    const runtime_history_allocator *allocator);
void runtime_history_destroy(runtime_history *history);

void runtime_history_get_status(
    const runtime_history *history,
    runtime_history_status *out_status);
bool runtime_history_has_active_record(const runtime_history *history);

bool runtime_history_begin_record(
    runtime_history *history,
    const runtime_history_begin *begin);
bool runtime_history_append_access(
    runtime_history *history,
    c6510_bus_access_kind kind,
    uint16_t address,
    uint8_t value,
    uint64_t machine_cycle);
/* Hot observer entry: recorder availability/recording were established when
   the observer was installed; only an active execution record is required. */
bool runtime_history_append_observed_access(
    runtime_history *history,
    c6510_bus_access_kind kind,
    uint16_t address,
    uint8_t value,
    uint64_t machine_cycle);
bool runtime_history_complete_record(runtime_history *history);
bool runtime_history_seal_partial(runtime_history *history);
bool runtime_history_append_marker(
    runtime_history *history,
    uint16_t marker_kind,
    uint32_t arg0,
    uint32_t arg1,
    uint64_t machine_cycle);

bool runtime_history_stop(runtime_history *history, uint64_t machine_cycle);
bool runtime_history_resume(runtime_history *history, uint64_t machine_cycle);
bool runtime_history_clear(runtime_history *history, uint64_t machine_cycle);
bool runtime_history_clear_for_state_load(
    runtime_history *history,
    uint64_t machine_cycle);
bool runtime_history_transition_timeline(runtime_history *history);
bool runtime_history_set_timeline(runtime_history *history, uint32_t timeline);
/* Logical floor: status/first treat `id` as the new oldest retained record. */
bool runtime_history_retain_from(
    runtime_history *history,
    uint64_t epoch,
    uint64_t id);

bool runtime_history_lookup(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record);
bool runtime_history_first(
    const runtime_history *history,
    runtime_history_record *out_record);
bool runtime_history_last(
    const runtime_history *history,
    runtime_history_record *out_record);
bool runtime_history_next(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record);
bool runtime_history_previous(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t id,
    runtime_history_record *out_record);

runtime_history_query_result runtime_history_find(
    const runtime_history *history,
    const runtime_history_query *query,
    uint64_t from_id,
    size_t limit,
    runtime_history_record *out_records,
    runtime_history_page *out_page,
    runtime_history_query_stats *out_stats);
runtime_history_query_result runtime_history_read(
    const runtime_history *history,
    uint64_t epoch,
    uint64_t anchor_id,
    size_t before,
    size_t after,
    runtime_history_record *out_records,
    size_t out_capacity,
    runtime_history_page *out_page);

/* Unit-test-only corruption hook. Production code must not call this. */
bool runtime_history_test_corrupt_record_size(
    runtime_history *history,
    uint64_t id,
    uint8_t access_count);

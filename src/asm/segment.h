// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    const char *segment_name;
    uint32_t segment_name_length;
    uint64_t segment_name_hash;
    uint16_t segment_start_address;
    uint16_t segment_output_address;
    int do_not_emit;
    int segment_init;
} SEGMENT;

typedef struct TARGET TARGET;
typedef struct TARGET {
    const char *name;
    int name_length;
    DYNARRAY segments;
    void *target_ctx;
    TARGET *prev_target;
    SEGMENT *active_segment;
} TARGET;

SEGMENT *segment_find(DYNARRAY *segments, const SEGMENT *seg);
TARGET *add_target(ASSEMBLER *as, void *target_ctx);

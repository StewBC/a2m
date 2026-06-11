// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    CPU cpu;
    A2_STATE state_flags;
    uint8_t b0;
    uint8_t b1;
    uint8_t b2;
} TRACE_DATA;

typedef struct {
    size_t trace_position;
    size_t trace_max_entries;
    uint32_t trace_on: 1;
    uint32_t trace_wrapped: 1;
    uint32_t trace_dirty: 1;
    uint32_t pad: 29;
    UTIL_FILE file;
    TRACE_DATA *trace_buffer;
} TRACE_LOG;

int rt_trace(RUNTIME *rt);
void rt_trace_off(RUNTIME *rt);
int rt_trace_init(RUNTIME *rt, const char *filename, size_t transaction);
void rt_trace_on(RUNTIME *rt);
void rt_trace_shutdown(RUNTIME *rt);

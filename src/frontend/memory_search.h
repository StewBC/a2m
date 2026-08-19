#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MEMORY_SEARCH_QUERY_MAX = 511,
    MEMORY_SEARCH_PATTERN_MAX = 255
};

typedef enum memory_search_mode {
    MEMORY_SEARCH_STRING = 0,
    MEMORY_SEARCH_HEX
} memory_search_mode;

typedef struct memory_search_pattern {
    uint8_t bytes[MEMORY_SEARCH_PATTERN_MAX];
    size_t length;
    bool ignore_case;
} memory_search_pattern;

bool memory_search_parse(
    const char *query,
    memory_search_mode mode,
    bool ignore_case,
    memory_search_pattern *out,
    char *error,
    size_t error_size);

/* Search the circular 64K Apple address space, beginning one byte beyond
 * start_address in the requested direction. valid may be NULL for a fully
 * populated view. */
bool memory_search_find(
    const uint8_t *bytes,
    const uint8_t *valid,
    const memory_search_pattern *pattern,
    uint16_t start_address,
    bool reverse,
    uint16_t *out_address);

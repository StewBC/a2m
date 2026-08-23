#pragma once

#include "runtime_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum frontend_runtime_state {
    FRONTEND_RUNTIME_STATE_UNKNOWN = 0,
    FRONTEND_RUNTIME_STATE_RUNNING,
    FRONTEND_RUNTIME_STATE_PAUSED,
    FRONTEND_RUNTIME_STATE_ERROR
} frontend_runtime_state;

/* product_label: model string (e.g. "//e Enhanced"); turbo is milli-MHz (0=max). */
void frontend_format_window_title(
    char *out,
    size_t out_size,
    const char *product_label,
    uint32_t turbo_multiplier,
    frontend_runtime_state state,
    runtime_stop_reason stop_reason);

void frontend_format_window_title_ex(
    char *out,
    size_t out_size,
    const char *product_label,
    uint32_t turbo_multiplier,
    frontend_runtime_state state,
    runtime_stop_reason stop_reason,
    bool tm_forensic,
    uint64_t tm_focus_cycle,
    uint64_t tm_oldest_cycle,
    uint64_t tm_newest_cycle);

#include "window_title.h"

#include "runtime.h"

#include <stdio.h>
#include <string.h>

static const char *window_title_stop_reason_name(runtime_stop_reason reason)
{
    switch (reason) {
        case RUNTIME_STOP_REASON_RESET:         return "reset";
        case RUNTIME_STOP_REASON_PAUSE_COMMAND: return "pause";
        case RUNTIME_STOP_REASON_STEP:          return "step";
        case RUNTIME_STOP_REASON_RUN_COMPLETE:  return "run complete";
        case RUNTIME_STOP_REASON_BREAKPOINT:    return "breakpoint";
        case RUNTIME_STOP_REASON_BRK:           return "BRK";
        case RUNTIME_STOP_REASON_ERROR:         return "error";
        case RUNTIME_STOP_REASON_NONE:
        default:                                return "none";
    }
}

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
    uint64_t tm_newest_cycle)
{
    const char *label = "Apple II";
    char turbo[32];
    char state_text[80];

    if (out == NULL || out_size == 0) {
        return;
    }
    if (product_label != NULL && product_label[0] != '\0') {
        label = product_label;
    }
    /* turbo_multiplier is milli-MHz (0 = max). See runtime.h turbo encoding. */
    runtime_turbo_format_label(turbo_multiplier, turbo, sizeof(turbo));

    if (tm_forensic) {
        snprintf(
            state_text,
            sizeof(state_text),
            "TIME MACHINE %llu-%llu @ %llu",
            (unsigned long long)tm_oldest_cycle,
            (unsigned long long)tm_newest_cycle,
            (unsigned long long)tm_focus_cycle);
        snprintf(out, out_size, "a2m - %s - %s - %s", label, turbo, state_text);
        return;
    }

    switch (state) {
        case FRONTEND_RUNTIME_STATE_RUNNING:
            snprintf(state_text, sizeof(state_text), "Running");
            break;
        case FRONTEND_RUNTIME_STATE_PAUSED:
            snprintf(state_text, sizeof(state_text), "Paused (%s)",
                window_title_stop_reason_name(stop_reason));
            break;
        case FRONTEND_RUNTIME_STATE_ERROR:
            snprintf(state_text, sizeof(state_text), "Error");
            break;
        case FRONTEND_RUNTIME_STATE_UNKNOWN:
        default:
            snprintf(state_text, sizeof(state_text), "Unknown");
            break;
    }
    snprintf(out, out_size, "a2m - %s - %s - %s", label, turbo, state_text);
}

void frontend_format_window_title(
    char *out,
    size_t out_size,
    const char *product_label,
    uint32_t turbo_multiplier,
    frontend_runtime_state state,
    runtime_stop_reason stop_reason)
{
    frontend_format_window_title_ex(
        out,
        out_size,
        product_label,
        turbo_multiplier,
        state,
        stop_reason,
        false,
        0u,
        0u,
        0u);
}

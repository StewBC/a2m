#pragma once

#include "runtime_history.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* Scratch cap for find-option text (matches control line max). */
    RUNTIME_HISTORY_FIND_OPTIONS_MAX = 512
};

/*
 * Parse whitespace-separated key=value find options into a query.
 * Defaults: direction=backward, limit=64, from=default.
 * Unknown keys / bad values fail. Duplicate keys: last wins.
 * text may be NULL or empty (defaults only).
 */
bool runtime_history_parse_find_options(
    const char *text,
    runtime_history_query *query,
    runtime_history_from_kind *from_kind,
    uint64_t *from_id,
    uint16_t *limit);

/* NULL-terminated tables accepted by the parser (autocomplete / honesty). */
const char *const *runtime_history_find_option_keys(void);
const char *const *runtime_history_find_access_names(void);

#ifdef __cplusplus
}
#endif

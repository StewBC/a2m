#pragma once

#include "runtime_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Parse break-create / break-update definition text:
 *   <access> <address> [enabled=0|1] [end=<addr>] [actions=...] [counter=]
 *   [reset=] [swap-slot=0..7] [ram=map|main|aux] [c100=map|rom]
 *   [d000=map|lc1|lc2|rom]
 *   [when=<condition>]
 *   (main and ram are aliases of map)
 */
bool control_parse_breakpoint_definition(
    const char *text,
    runtime_breakpoint_definition *definition,
    char *error,
    size_t error_size);

/* Build newline-separated breakpoint records for a data breakpoints response.
 * Caller frees *out_payload. Returns false on allocation failure. */
bool control_format_breakpoints_payload(
    const runtime_breakpoint_snapshot *breakpoints,
    uint8_t **out_payload,
    size_t *out_payload_size,
    char *out_metadata,
    size_t metadata_size);

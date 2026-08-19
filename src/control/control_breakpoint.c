#include "control_breakpoint.h"

#include "runtime_breakpoint_condition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_u32_field(const char *value, uint32_t *out)
{
    char *end;
    unsigned long parsed;

    if (value == NULL || value[0] == '\0' || out == NULL) {
        return false;
    }
    parsed = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || parsed > 0xfffffffful) {
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static bool parse_u16_field(const char *value, uint16_t *out)
{
    char *end;
    unsigned long parsed;
    int base = 0;

    if (value == NULL || value[0] == '\0' || out == NULL) {
        return false;
    }
    if (value[0] == '$') {
        value++;
        base = 16;
    }
    parsed = strtoul(value, &end, base);
    if (end == value || *end != '\0' || parsed > 0xfffful) {
        return false;
    }
    *out = (uint16_t)parsed;
    return true;
}

static bool parse_bool_field(const char *value, uint8_t *out)
{
    if (value == NULL || out == NULL) {
        return false;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
        *out = 1u;
        return true;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
        *out = 0u;
        return true;
    }
    return false;
}

static bool parse_breakpoint_access(const char *token, uint32_t *out_access)
{
    if (token == NULL || out_access == NULL) {
        return false;
    }
    if (strcmp(token, "exec") == 0 || strcmp(token, "execute") == 0) {
        *out_access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        return true;
    }
    if (strcmp(token, "read") == 0 || strcmp(token, "load") == 0) {
        *out_access = RUNTIME_BREAKPOINT_ACCESS_READ;
        return true;
    }
    if (strcmp(token, "write") == 0 || strcmp(token, "store") == 0) {
        *out_access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
        return true;
    }
    if (strcmp(token, "read-write") == 0 || strcmp(token, "load-store") == 0) {
        *out_access = RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE;
        return true;
    }
    return false;
}

static bool parse_breakpoint_actions(const char *value, uint32_t *out_actions)
{
    uint32_t actions = 0;
    const char *cursor = value;
    bool saw_none = false;

    if (value == NULL || value[0] == '\0' || out_actions == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        const char *start = cursor;
        size_t length;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        length = (size_t)(cursor - start);
        if (length == 4 && strncmp(start, "none", length) == 0) {
            saw_none = true;
        } else if (length == 5 && strncmp(start, "break", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
        } else if (length == 4 && strncmp(start, "fast", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
        } else if (length == 4 && strncmp(start, "slow", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
        } else if (length == 4 && strncmp(start, "tron", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
        } else if (length == 5 && strncmp(start, "troff", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
        } else if (length == 4 && strncmp(start, "type", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        } else if (length == 4 && strncmp(start, "swap", length) == 0) {
            actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
        } else {
            return false;
        }
        if (*cursor == ',') {
            cursor++;
        }
    }
    if (saw_none && actions != 0) {
        return false;
    }
    *out_actions = actions;
    return saw_none || actions != 0;
}

bool control_parse_breakpoint_definition(
    const char *text,
    runtime_breakpoint_definition *definition,
    char *error,
    size_t error_size)
{
    char buffer[1024];
    char *token;

    if (error != NULL && error_size > 0u) {
        error[0] = '\0';
    }
    if (text == NULL || definition == NULL) {
        return false;
    }
    snprintf(buffer, sizeof(buffer), "%s", text);
    memset(definition, 0, sizeof(*definition));
    definition->enabled = 1u;
    definition->access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    definition->actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    definition->reset_count = 1u;
    definition->swap_slot = 6u;

    token = strtok(buffer, " \t");
    if (token == NULL || !parse_breakpoint_access(token, &definition->access)) {
        return false;
    }
    token = strtok(NULL, " \t");
    if (token == NULL || !parse_u16_field(token, &definition->start_address)) {
        return false;
    }
    definition->end_address = definition->start_address;

    while ((token = strtok(NULL, " \t")) != NULL) {
        char *eq = strchr(token, '=');
        char *key;
        char *value;
        if (eq == NULL) {
            return false;
        }
        *eq = '\0';
        key = token;
        value = eq + 1;
        if (strcmp(key, "enabled") == 0) {
            if (!parse_bool_field(value, &definition->enabled)) {
                return false;
            }
        } else if (strcmp(key, "end") == 0) {
            if (!parse_u16_field(value, &definition->end_address)) {
                return false;
            }
            definition->has_end_address = 1u;
        } else if (strcmp(key, "actions") == 0) {
            if (!parse_breakpoint_actions(value, &definition->actions)) {
                return false;
            }
        } else if (strcmp(key, "counter") == 0) {
            if (!parse_u32_field(value, &definition->initial_count)) {
                return false;
            }
            definition->use_counter = 1u;
        } else if (strcmp(key, "reset") == 0) {
            if (!parse_u32_field(value, &definition->reset_count)) {
                return false;
            }
        } else if (strcmp(key, "swap-slot") == 0 || strcmp(key, "swap_slot") == 0) {
            uint32_t slot;
            if (!parse_u32_field(value, &slot) || slot > 7u) {
                return false;
            }
            definition->swap_slot = (uint8_t)slot;
        } else if (strcmp(key, "mapping") == 0) {
            /* Compatibility shorthand for one Apple mapping axis. */
            if (strcmp(value, "map") == 0 || strcmp(value, "ram") == 0) {
                definition->mapping = 0u;
            } else if (strcmp(value, "main") == 0) {
                vf_set_ram(&definition->mapping, A2SEL48K_MAIN);
            } else if (strcmp(value, "aux") == 0) {
                vf_set_ram(&definition->mapping, A2SEL48K_AUX);
            } else if (strcmp(value, "lc1") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_LC_B1);
            } else if (strcmp(value, "lc2") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_LC_B2);
            } else if (strcmp(value, "rom") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_ROM);
            } else {
                return false;
            }
        } else if (strcmp(key, "ram") == 0) {
            if (strcmp(value, "map") == 0) {
                vf_set_ram(&definition->mapping, A2SEL48K_MAPPED);
            } else if (strcmp(value, "main") == 0) {
                vf_set_ram(&definition->mapping, A2SEL48K_MAIN);
            } else if (strcmp(value, "aux") == 0) {
                vf_set_ram(&definition->mapping, A2SEL48K_AUX);
            } else {
                return false;
            }
        } else if (strcmp(key, "c100") == 0) {
            if (strcmp(value, "map") == 0) {
                vf_set_c100(&definition->mapping, A2SELC100_MAPPED);
            } else if (strcmp(value, "rom") == 0) {
                vf_set_c100(&definition->mapping, A2SELC100_ROM);
            } else {
                return false;
            }
        } else if (strcmp(key, "d000") == 0) {
            if (strcmp(value, "map") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_MAPPED);
            } else if (strcmp(value, "lc1") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_LC_B1);
            } else if (strcmp(value, "lc2") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_LC_B2);
            } else if (strcmp(value, "rom") == 0) {
                vf_set_d000(&definition->mapping, A2SELD000_ROM);
            } else {
                return false;
            }
        } else if (strcmp(key, "when") == 0) {
            if (!runtime_bp_condition_parse(
                    value, &definition->condition, error, error_size)) {
                return false;
            }
        } else {
            return false;
        }
    }

    if ((definition->access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0 &&
        runtime_bp_condition_uses_value(&definition->condition)) {
        if (error != NULL && error_size > 0u) {
            snprintf(
                error,
                error_size,
                "`value` has no meaning on an exec breakpoint");
        }
        return false;
    }
    return true;
}

bool control_format_breakpoints_payload(
    const runtime_breakpoint_snapshot *breakpoints,
    uint8_t **out_payload,
    size_t *out_payload_size,
    char *out_metadata,
    size_t metadata_size)
{
    uint8_t *payload;
    char *cursor;
    size_t payload_size;
    size_t used = 0;
    uint16_t i;

    if (breakpoints == NULL || out_payload == NULL || out_payload_size == NULL) {
        return false;
    }
    payload_size = 1u + (size_t)RUNTIME_BREAKPOINT_SNAPSHOT_MAX * 384u;
    payload = (uint8_t *)malloc(payload_size);
    if (payload == NULL) {
        return false;
    }
    cursor = (char *)payload;
    for (i = 0; i < breakpoints->count && i < RUNTIME_BREAKPOINT_SNAPSHOT_MAX; i++) {
        const runtime_breakpoint_snapshot_entry *entry = &breakpoints->entries[i];
        char condition_text[RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX];
        int written;

        if (!runtime_bp_condition_format(
                &entry->condition, condition_text, sizeof(condition_text))) {
            condition_text[0] = '\0';
        }
        written = snprintf(
            cursor + used,
            payload_size - used,
            "id=%u enabled=%u start=%04X end=%04X has_end=%u "
            "access=%u ram=%u c100=%u d000=%u "
            "actions=%u use_counter=%u hits=%u initial=%u reset=%u counter=%u "
            "swap_slot=%u swap_param=%d swap_relative=%u cond=%u when=%s\n",
            entry->id,
            entry->enabled,
            entry->start_address,
            entry->end_address,
            entry->has_end_address,
            (unsigned)entry->access,
            (unsigned)vf_get_ram(entry->mapping),
            (unsigned)vf_get_c100(entry->mapping),
            (unsigned)vf_get_d000(entry->mapping),
            entry->actions,
            entry->use_counter,
            entry->current_hits,
            entry->initial_count,
            entry->reset_count,
            entry->counter,
            entry->swap_slot,
            entry->swap_param,
            entry->swap_relative,
            (unsigned)entry->condition.term_count,
            condition_text);
        if (written < 0 || (size_t)written >= payload_size - used) {
            break;
        }
        used += (size_t)written;
    }
    if (out_metadata != NULL && metadata_size > 0u) {
        snprintf(out_metadata, metadata_size, "count=%u", breakpoints->count);
    }
    *out_payload = payload;
    *out_payload_size = used;
    return true;
}

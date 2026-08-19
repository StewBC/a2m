#include "runtime_breakpoint_ini.h"

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RUNTIME_BREAKPOINT_KEY_MAX = 64,
    RUNTIME_BREAKPOINT_VALUE_MAX = 1024,
};

static bool runtime_ini_streq(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0') {
        if (tolower((unsigned char)*lhs) != tolower((unsigned char)*rhs)) {
            return false;
        }
        lhs++;
        rhs++;
    }

    return *lhs == '\0' && *rhs == '\0';
}

static char *runtime_ini_trim(char *value) {
    char *end;

    while (isspace((unsigned char)*value)) {
        value++;
    }

    end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return value;
}

static bool runtime_ini_parse_hex16(const char *text, uint16_t *out) {
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return false;
    }

    if (*text == '$') {
        text++;
    }

    value = strtoul(text, &end, 16);
    if (end == text || *end != '\0' || value > 0xfffful) {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool runtime_ini_parse_u32(const char *text, uint32_t *out) {
    char *end;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }

    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0xfffffffful) {
        return false;
    }

    *out = (uint32_t)value;
    return true;
}

/* Parses [+|-]N into swap_param and swap_relative.
   Returns false if the text is empty or non-numeric. 0 is always a no-op. */
static bool runtime_ini_parse_swap_param(const char *text, int32_t *out_param, uint8_t *out_relative) {
    bool relative = false;
    bool negative = false;
    unsigned long value;
    char *end;

    if (text == NULL || *text == '\0') {
        *out_param = 0;
        *out_relative = 0;
        return true;
    }

    if (*text == '+') {
        relative = true;
        text++;
    } else if (*text == '-') {
        relative = true;
        negative = true;
        text++;
    }

    if (*text == '\0') {
        return false;
    }

    value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > 0x7ffffffful) {
        return false;
    }

    *out_param = negative ? -(int32_t)value : (int32_t)value;
    *out_relative = relative ? 1u : 0u;
    return true;
}

typedef struct runtime_ini_breakpoint_parse_state {
    bool saw_access;
    bool saw_action;
    bool saw_reset;
} runtime_ini_breakpoint_parse_state;

static void runtime_ini_warn(const char *key, const char *message) {
    fprintf(stderr, "warning: breakpoint `%s`: %s\n", key, message);
}

static void runtime_ini_strip_suffix(char *address) {
    char *dot = strrchr(address, '.');
    char *p;

    if (dot == NULL || dot[1] == '\0') {
        return;
    }

    for (p = dot + 1; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return;
        }
    }

    *dot = '\0';
}

static bool runtime_ini_parse_address_key(
    const char *key,
    uint16_t *start,
    uint16_t *end,
    bool *has_end) {
    char buffer[RUNTIME_BREAKPOINT_KEY_MAX];
    char *dash;

    if (strncmp(key, "break.", 6) != 0) {
        return false;
    }

    snprintf(buffer, sizeof(buffer), "%s", key + 6);
    runtime_ini_strip_suffix(buffer);
    dash = strchr(buffer, '-');
    if (dash != NULL) {
        *dash = '\0';
        if (!runtime_ini_parse_hex16(buffer, start) ||
            !runtime_ini_parse_hex16(dash + 1, end)) {
            return false;
        }
        *has_end = true;
        return true;
    }

    if (!runtime_ini_parse_hex16(buffer, start)) {
        return false;
    }

    *end = *start;
    *has_end = false;
    return true;
}

static bool runtime_ini_parse_breakpoint_item(
    const char *key,
    char *item,
    runtime_breakpoint_definition *definition,
    runtime_ini_breakpoint_parse_state *state) {
    if (runtime_ini_streq(item, "execute")) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        state->saw_access = true;
    } else if (runtime_ini_streq(item, "read")) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ;
        state->saw_access = true;
    } else if (runtime_ini_streq(item, "write")) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_WRITE;
        state->saw_access = true;
    } else if (runtime_ini_streq(item, "access")) {
        definition->access |= RUNTIME_BREAKPOINT_ACCESS_READ | RUNTIME_BREAKPOINT_ACCESS_WRITE;
        state->saw_access = true;
    } else if (runtime_ini_streq(item, "map")) {
        definition->mapping = 0u;
    } else if (runtime_ini_streq(item, "main")) {
        vf_set_ram(&definition->mapping, A2SEL48K_MAIN);
    } else if (runtime_ini_streq(item, "aux")) {
        vf_set_ram(&definition->mapping, A2SEL48K_AUX);
    } else if (runtime_ini_streq(item, "c100map") ||
               runtime_ini_streq(item, "c100mapped")) {
        vf_set_c100(&definition->mapping, A2SELC100_MAPPED);
    } else if (runtime_ini_streq(item, "c100rom")) {
        vf_set_c100(&definition->mapping, A2SELC100_ROM);
    } else if (runtime_ini_streq(item, "d000map") ||
               runtime_ini_streq(item, "d000mapped")) {
        vf_set_d000(&definition->mapping, A2SELD000_MAPPED);
    } else if (runtime_ini_streq(item, "lc1")) {
        vf_set_d000(&definition->mapping, A2SELD000_LC_B1);
    } else if (runtime_ini_streq(item, "lc2")) {
        vf_set_d000(&definition->mapping, A2SELD000_LC_B2);
    } else if (runtime_ini_streq(item, "rom")) {
        vf_set_d000(&definition->mapping, A2SELD000_ROM);
    } else if (runtime_ini_streq(item, "ram")) {
        /* Legacy token; treat as Map on Apple. */
        definition->mapping = 0u;
    } else if (runtime_ini_streq(item, "break")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_BREAK;
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "fast")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_FAST;
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "slow")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SLOW;
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "tron")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
        definition->tron_path[0] = '\0';
        state->saw_action = true;
    } else if (strncmp(item, "tron=", 5) == 0) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TRON;
        snprintf(definition->tron_path, sizeof(definition->tron_path), "%s", item + 5);
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "troff")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TROFF;
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "type")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        definition->type_text[0] = '\0';
        state->saw_action = true;
    } else if (strncmp(item, "type=", 5) == 0) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        snprintf(definition->type_text, sizeof(definition->type_text), "%s", item + 5);
        state->saw_action = true;
    } else if (runtime_ini_streq(item, "swap")) {
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
        definition->swap_param = 0;
        definition->swap_relative = 0;
        state->saw_action = true;
    } else if (strncmp(item, "swap=", 5) == 0) {
        int32_t param = 0;
        uint8_t relative = 0;
        if (!runtime_ini_parse_swap_param(item + 5, &param, &relative)) {
            runtime_ini_warn(key, "invalid swap parameter");
            return false;
        }
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_SWAP;
        definition->swap_param = param;
        definition->swap_relative = relative;
        state->saw_action = true;
    } else if (strncmp(item, "swap-slot=", 10) == 0 ||
               strncmp(item, "swap_slot=", 10) == 0) {
        uint32_t slot;
        if (!runtime_ini_parse_u32(item + 10, &slot) || slot > 7u) {
            runtime_ini_warn(key, "invalid swap slot (expected 0..7)");
            return false;
        }
        definition->swap_slot = (uint8_t)slot;
    } else if (runtime_ini_streq(item, "enabled")) {
        definition->enabled = 1;
    } else if (runtime_ini_streq(item, "disabled")) {
        definition->enabled = 0;
    } else if (strncmp(item, "count=", 6) == 0) {
        if (!runtime_ini_parse_u32(item + 6, &definition->initial_count)) {
            runtime_ini_warn(key, "invalid count");
            return false;
        }
        definition->use_counter = 1;
    } else if (strncmp(item, "reset=", 6) == 0) {
        if (!runtime_ini_parse_u32(item + 6, &definition->reset_count)) {
            runtime_ini_warn(key, "invalid reset");
            return false;
        }
        state->saw_reset = true;
    } else if (strncmp(item, "when=", 5) == 0) {
        char condition_error[128];
        if (!runtime_bp_condition_parse(
                item + 5,
                &definition->condition,
                condition_error,
                sizeof(condition_error))) {
            runtime_ini_warn(key, "invalid condition");
            return false;
        }
    } else if (*item != '\0') {
        runtime_ini_warn(key, "unknown keyword ignored");
    }

    return true;
}

static bool runtime_ini_parse_breakpoint_items(
    const char *key,
    char *text,
    runtime_breakpoint_definition *definition,
    runtime_ini_breakpoint_parse_state *state) {
    char *token = strtok(text, ",");

    while (token != NULL) {
        char *item = runtime_ini_trim(token);
        if (!runtime_ini_parse_breakpoint_item(key, item, definition, state)) {
            return false;
        }
        token = strtok(NULL, ",");
    }

    return true;
}

static bool runtime_ini_find_type_assignment(char *text, char **token_start, char **value_start) {
    char *start = text;

    while (start != NULL) {
        char *item = start;
        char *comma;

        while (isspace((unsigned char)*item)) {
            item++;
        }
        if (strncmp(item, "type=", 5) == 0) {
            *token_start = start;
            *value_start = item + 5;
            return true;
        }

        comma = strchr(start, ',');
        start = comma != NULL ? comma + 1 : NULL;
    }

    return false;
}

static bool runtime_ini_parse_type_trailing_counter(
    const char *key,
    char *type_value,
    runtime_breakpoint_definition *definition,
    runtime_ini_breakpoint_parse_state *state) {
    char *end = type_value + strlen(type_value);

    while (end > type_value) {
        char *comma = end;
        char tail[RUNTIME_BREAKPOINT_VALUE_MAX];
        char *item;

        while (comma > type_value && comma[-1] != ',') {
            comma--;
        }
        if (comma == type_value) {
            break;
        }

        snprintf(tail, sizeof(tail), "%.*s", (int)(end - comma), comma);
        item = runtime_ini_trim(tail);
        if (strncmp(item, "count=", 6) != 0 && strncmp(item, "reset=", 6) != 0) {
            break;
        }
        if (!runtime_ini_parse_breakpoint_item(key, item, definition, state)) {
            return false;
        }

        comma[-1] = '\0';
        end = comma - 1;
    }

    return true;
}

static bool runtime_ini_parse_breakpoint(
    const char *key,
    const char *value,
    runtime_breakpoint_definition *definition) {
    char buffer[RUNTIME_BREAKPOINT_VALUE_MAX];
    runtime_ini_breakpoint_parse_state state = {0};
    char *type_token = NULL;
    char *type_value = NULL;
    bool has_end = false;

    memset(definition, 0, sizeof(*definition));
    definition->enabled = 1;
    definition->swap_slot = 6u;

    if (!runtime_ini_parse_address_key(
            key,
            &definition->start_address,
            &definition->end_address,
            &has_end)) {
        runtime_ini_warn(key, "invalid address key");
        return false;
    }
    definition->has_end_address = has_end ? 1u : 0u;

    snprintf(buffer, sizeof(buffer), "%s", value != NULL ? value : "");

    if (runtime_ini_find_type_assignment(buffer, &type_token, &type_value)) {
        if (type_token > buffer && type_token[-1] == ',') {
            type_token[-1] = '\0';
        } else {
            *type_token = '\0';
        }
        if (!runtime_ini_parse_breakpoint_items(key, buffer, definition, &state)) {
            return false;
        }
        if (!runtime_ini_parse_type_trailing_counter(key, type_value, definition, &state)) {
            return false;
        }
        definition->actions |= RUNTIME_BREAKPOINT_ACTION_TYPE;
        snprintf(definition->type_text, sizeof(definition->type_text), "%s", type_value);
        state.saw_action = true;
    } else {
        if (!runtime_ini_parse_breakpoint_items(key, buffer, definition, &state)) {
            return false;
        }
    }

    if (definition->use_counter && !state.saw_reset) {
        definition->reset_count = definition->initial_count;
    }

    if (!state.saw_access || !state.saw_action) {
        runtime_ini_warn(key, "missing access or action");
        return false;
    }

    return true;
}

static bool runtime_add_loaded_breakpoint(runtime *rt, const runtime_breakpoint_definition *definition) {
    runtime_breakpoint *breakpoint;

    if (rt->breakpoint_count >= RUNTIME_BREAKPOINT_CAPACITY) {
        runtime_ini_warn("DEBUG", "breakpoint table is full");
        return false;
    }

    if (rt->next_breakpoint_id == 0) {
        rt->next_breakpoint_id = 1;
    }

    breakpoint = &rt->breakpoints[rt->breakpoint_count];
    breakpoint->id = rt->next_breakpoint_id++;
    breakpoint->enabled = definition->enabled != 0;
    breakpoint->start_address = definition->start_address;
    breakpoint->end_address = definition->has_end_address ?
        definition->end_address :
        definition->start_address;
    breakpoint->has_end_address = definition->has_end_address != 0;
    breakpoint->access_mask = definition->access;
    breakpoint->mapping = definition->mapping;
    breakpoint->action_mask = definition->actions;
    breakpoint->use_counter = definition->use_counter != 0;
    breakpoint->initial_count = definition->initial_count;
    breakpoint->reset_count = definition->reset_count;
    breakpoint->counter = definition->initial_count;
    breakpoint->current_hits = 0;
    breakpoint->swap_slot = definition->swap_slot;
    breakpoint->swap_param = definition->swap_param;
    breakpoint->swap_relative = definition->swap_relative;
    snprintf(breakpoint->tron_path, sizeof(breakpoint->tron_path), "%s", definition->tron_path);
    snprintf(breakpoint->type_text, sizeof(breakpoint->type_text), "%s", definition->type_text);
    breakpoint->condition = definition->condition;
    if (!runtime_bp_condition_is_valid(&breakpoint->condition)) {
        memset(&breakpoint->condition, 0, sizeof(breakpoint->condition));
    }
    rt->breakpoint_count++;
    return true;
}

bool runtime_load_breakpoints_from_ini(runtime *rt) {
    config *cfg;
    int count;
    int i;

    if (rt == NULL || !rt->use_ini || rt->ini_path == NULL) {
        return true;
    }

    cfg = config_load(rt->ini_path);
    if (cfg == NULL) {
        return true;
    }

    count = config_entry_count(cfg);
    for (i = 0; i < count; ++i) {
        const char *section;
        const char *key;
        const char *value;
        runtime_breakpoint_definition definition;

        if (!config_entry_at(cfg, i, &section, &key, &value)) {
            continue;
        }

        if (strcmp(section, "DEBUG") != 0 || strncmp(key, "break.", 6) != 0) {
            continue;
        }

        if (runtime_ini_parse_breakpoint(key, value, &definition)) {
            runtime_add_loaded_breakpoint(rt, &definition);
        }
    }

    config_destroy(cfg);
    return true;
}

static void runtime_format_breakpoint_key(
    const runtime_breakpoint *breakpoint,
    int suffix,
    char *out,
    size_t out_size) {
    char base[RUNTIME_BREAKPOINT_KEY_MAX];

    if (breakpoint->has_end_address) {
        snprintf(
            base,
            sizeof(base),
            "break.%04X-%04X",
            breakpoint->start_address,
            breakpoint->end_address);
    } else {
        snprintf(base, sizeof(base), "break.%04X", breakpoint->start_address);
    }

    if (suffix > 0) {
        snprintf(out, out_size, "%s.%d", base, suffix);
    } else {
        snprintf(out, out_size, "%s", base);
    }
}

static bool runtime_breakpoint_same_base(
    const runtime_breakpoint *lhs,
    const runtime_breakpoint *rhs) {
    return lhs->start_address == rhs->start_address &&
        lhs->end_address == rhs->end_address &&
        lhs->has_end_address == rhs->has_end_address;
}

static int runtime_breakpoint_suffix_for_index(runtime *rt, size_t index) {
    int suffix = 0;
    size_t i;

    for (i = 0; i < index; ++i) {
        if (runtime_breakpoint_same_base(&rt->breakpoints[i], &rt->breakpoints[index])) {
            suffix++;
        }
    }

    return suffix;
}

static void runtime_append_token(char *buffer, size_t size, const char *token) {
    if (buffer[0] != '\0') {
        strncat(buffer, ",", size - strlen(buffer) - 1u);
    }
    strncat(buffer, token, size - strlen(buffer) - 1u);
}

static void runtime_format_breakpoint_value(
    const runtime_breakpoint *breakpoint,
    char *out,
    size_t out_size) {
    char counter[32];

    out[0] = '\0';
    if (!breakpoint->enabled) {
        runtime_append_token(out, out_size, "disabled");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0) {
        runtime_append_token(out, out_size, "execute");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_READ) != 0) {
        runtime_append_token(out, out_size, "read");
    }
    if ((breakpoint->access_mask & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0) {
        runtime_append_token(out, out_size, "write");
    }

    if (vf_get_ram(breakpoint->mapping) == A2SEL48K_MAIN) {
        runtime_append_token(out, out_size, "main");
    } else if (vf_get_ram(breakpoint->mapping) == A2SEL48K_AUX) {
        runtime_append_token(out, out_size, "aux");
    } else {
        runtime_append_token(out, out_size, "map");
    }
    if (vf_get_c100(breakpoint->mapping) == A2SELC100_ROM) {
        runtime_append_token(out, out_size, "c100rom");
    }
    if (vf_get_d000(breakpoint->mapping) == A2SELD000_LC_B1) {
        runtime_append_token(out, out_size, "lc1");
    } else if (vf_get_d000(breakpoint->mapping) == A2SELD000_LC_B2) {
        runtime_append_token(out, out_size, "lc2");
    } else if (vf_get_d000(breakpoint->mapping) == A2SELD000_ROM) {
        runtime_append_token(out, out_size, "rom");
    }

    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
        runtime_append_token(out, out_size, "break");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_FAST) != 0) {
        runtime_append_token(out, out_size, "fast");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SLOW) != 0) {
        runtime_append_token(out, out_size, "slow");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TRON) != 0) {
        if (breakpoint->tron_path[0] != '\0') {
            char tron_tok[RUNTIME_BREAKPOINT_TRON_PATH_MAX + 8];
            snprintf(tron_tok, sizeof(tron_tok), "tron=%s", breakpoint->tron_path);
            runtime_append_token(out, out_size, tron_tok);
        } else {
            runtime_append_token(out, out_size, "tron");
        }
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TROFF) != 0) {
        runtime_append_token(out, out_size, "troff");
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0) {
        char slot_tok[24];
        snprintf(slot_tok, sizeof(slot_tok), "swap-slot=%u", breakpoint->swap_slot);
        runtime_append_token(out, out_size, slot_tok);
        if (breakpoint->swap_param != 0) {
            char swap_tok[32];
            if (breakpoint->swap_relative) {
                snprintf(swap_tok, sizeof(swap_tok), "swap=%+d", breakpoint->swap_param);
            } else {
                snprintf(swap_tok, sizeof(swap_tok), "swap=%d", breakpoint->swap_param);
            }
            runtime_append_token(out, out_size, swap_tok);
        } else {
            runtime_append_token(out, out_size, "swap");
        }
    }
    if ((breakpoint->action_mask & RUNTIME_BREAKPOINT_ACTION_TYPE) != 0) {
        if (breakpoint->type_text[0] != '\0') {
            char type_tok[RUNTIME_BREAKPOINT_TYPE_TEXT_MAX + 8];
            snprintf(type_tok, sizeof(type_tok), "type=%s", breakpoint->type_text);
            runtime_append_token(out, out_size, type_tok);
        } else {
            runtime_append_token(out, out_size, "type");
        }
    }

    if (breakpoint->use_counter) {
        snprintf(counter, sizeof(counter), "count=%u", breakpoint->initial_count);
        runtime_append_token(out, out_size, counter);
        snprintf(counter, sizeof(counter), "reset=%u", breakpoint->reset_count);
        runtime_append_token(out, out_size, counter);
    }

    if (breakpoint->condition.term_count != 0u) {
        char condition_text[RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX];
        char condition_token[RUNTIME_BREAKPOINT_CONDITION_TEXT_MAX + 8];

        if (runtime_bp_condition_format(
                &breakpoint->condition, condition_text, sizeof(condition_text))) {
            /* Terms are separated with ';' here: this value is itself a
               comma-separated item list, and the parser accepts both. */
            char *scan;
            for (scan = condition_text; *scan != '\0'; ++scan) {
                if (*scan == ',') {
                    *scan = ';';
                }
            }
            snprintf(condition_token, sizeof(condition_token),
                     "when=%s", condition_text);
            runtime_append_token(out, out_size, condition_token);
        }
    }
}

bool runtime_save_breakpoints_to_ini(runtime *rt) {
    config *cfg;
    size_t i;
    bool ok;

    /* Caller gates on saveini/remember/nosaveini. Requires ini_path. */
    if (rt == NULL || rt->ini_path == NULL || rt->ini_path[0] == '\0') {
        return true;
    }

    cfg = config_load(rt->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    config_remove_prefix(cfg, "DEBUG", "break.");
    for (i = 0; i < rt->breakpoint_count; ++i) {
        char key[RUNTIME_BREAKPOINT_KEY_MAX];
        char value[RUNTIME_BREAKPOINT_VALUE_MAX];
        int suffix = runtime_breakpoint_suffix_for_index(rt, i);

        runtime_format_breakpoint_key(&rt->breakpoints[i], suffix, key, sizeof(key));
        runtime_format_breakpoint_value(&rt->breakpoints[i], value, sizeof(value));
        config_set(cfg, "DEBUG", key, value);
    }

    ok = config_save(cfg, rt->ini_path);
    config_destroy(cfg);
    return ok;
}

#include "forensics_view.h"

#include <SDL.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    FORENSICS_SEL_NONE = UINT_MAX,
    /* Match frontend_history_verb without including frontend.h. */
    FR_VERB_FIND = 1,
    FR_VERB_NEXT = 2,
    FR_VERB_READ = 3,
    FR_VERB_INFO = 4,
    FR_VERB_CLOSE = 5
};

void forensics_view_init(frontend_forensics_state *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->sel_logical_first = FORENSICS_SEL_NONE;
    state->sel_logical_last = FORENSICS_SEL_NONE;
    snprintf(state->status, sizeof(state->status), "ready");
}

static void forensics_free_logical(frontend_forensics_state *state)
{
    unsigned i;
    if (state == NULL) {
        return;
    }
    for (i = 0u; i < state->logical_count; ++i) {
        free(state->logical[i].text);
        state->logical[i].text = NULL;
    }
    state->logical_count = 0u;
    state->display_count = 0u;
}

void forensics_view_clear_transcript(frontend_forensics_state *state)
{
    if (state == NULL) {
        return;
    }
    forensics_free_logical(state);
    state->sel_logical_first = FORENSICS_SEL_NONE;
    state->sel_logical_last = FORENSICS_SEL_NONE;
    state->has_land_selection = false;
    state->selected_cycle = 0u;
    state->selected_id = 0u;
    state->line_truncated = false;
    forensics_view_set_status(state, "transcript cleared");
}

void forensics_view_set_status(frontend_forensics_state *state, const char *text)
{
    if (state == NULL) {
        return;
    }
    if (text == NULL) {
        state->status[0] = '\0';
        return;
    }
    snprintf(state->status, sizeof(state->status), "%s", text);
}

void forensics_view_open(
    frontend_forensics_state *state,
    frontend_forensics_entry entry,
    bool crt_was_running)
{
    if (state == NULL) {
        return;
    }
    state->open = true;
    state->entry = entry;
    state->crt_was_running =
        (entry == FRONTEND_FORENSICS_ENTRY_CRT) && crt_was_running;
    state->query_focus_pending = true;
    state->request_close = false;
    state->request_host_pause = true;
    state->request_submit = false;
    state->query_rewrite_pending = false;
    state->land_confirm_open = false;
    state->land_confirm_exact = false;
    state->request_land = false;
    state->request_land_exact = false;
    state->request_land_enter = false;
    state->pending_land_cycle = 0u;
    state->land_awaiting_focus = false;
    state->land_awaiting_exact = false;
    state->land_requested_cycle = 0u;
    state->request_leave_debugger = false;
    state->query_history_index = 0u;
    forensics_view_set_status(state, "paused — querying…");
}

void forensics_view_close(frontend_forensics_state *state)
{
    if (state == NULL || !state->open) {
        return;
    }
    state->open = false;
    state->query_focus_pending = false;
    state->request_close = false;
    state->request_host_pause = false;
    state->request_submit = false;
    state->land_confirm_open = false;
    state->land_confirm_exact = false;
    state->request_land = false;
    state->request_land_exact = false;
    state->request_land_enter = false;
    state->land_awaiting_focus = false;
    state->land_awaiting_exact = false;
    state->request_leave_debugger = false;
    state->entry = FRONTEND_FORENSICS_ENTRY_DEBUGGER;
    state->crt_was_running = false;
}

bool forensics_view_is_open(const frontend_forensics_state *state)
{
    return state != NULL && state->open;
}

frontend_forensics_entry forensics_view_entry(const frontend_forensics_state *state)
{
    if (state == NULL) {
        return FRONTEND_FORENSICS_ENTRY_DEBUGGER;
    }
    return state->entry;
}

bool forensics_view_crt_was_running(const frontend_forensics_state *state)
{
    return state != NULL && state->crt_was_running;
}

frontend_forensics_leave_result forensics_view_leave_to_entry(
    frontend_forensics_state *state)
{
    frontend_forensics_leave_result result;
    result.show_debugger = true;
    result.resume_machine = false;
    if (state == NULL || !state->open) {
        return result;
    }
    if (state->entry == FRONTEND_FORENSICS_ENTRY_CRT) {
        result.show_debugger = false;
        result.resume_machine = state->crt_was_running;
    }
    forensics_view_close(state);
    return result;
}

frontend_forensics_leave_result forensics_view_leave_to_debugger(
    frontend_forensics_state *state)
{
    frontend_forensics_leave_result result;
    result.show_debugger = true;
    result.resume_machine = false;
    if (state != NULL && state->open) {
        forensics_view_close(state);
    }
    return result;
}

bool forensics_view_query_history_prev(frontend_forensics_state *state)
{
    if (state == NULL || state->query_history_count == 0u) {
        return false;
    }
    if (state->query_history_index < state->query_history_count) {
        state->query_history_index++;
    }
    snprintf(
        state->query,
        sizeof(state->query),
        "%s",
        state->query_history[state->query_history_index - 1u]);
    return true;
}

bool forensics_view_query_history_next(frontend_forensics_state *state)
{
    if (state == NULL || state->query_history_index == 0u) {
        return false;
    }
    state->query_history_index--;
    if (state->query_history_index == 0u) {
        state->query[0] = '\0';
    } else {
        snprintf(
            state->query,
            sizeof(state->query),
            "%s",
            state->query_history[state->query_history_index - 1u]);
    }
    return true;
}

void forensics_view_query_history_push(
    frontend_forensics_state *state,
    const char *text)
{
    unsigned i;
    if (state == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    if (state->query_history_count > 0u &&
        strcmp(state->query_history[0], text) == 0) {
        state->query_history_index = 0u;
        return;
    }
    if (state->query_history_count < FRONTEND_FR_QUERY_HISTORY) {
        state->query_history_count++;
    }
    for (i = state->query_history_count - 1u; i > 0u; --i) {
        memcpy(
            state->query_history[i],
            state->query_history[i - 1u],
            FRONTEND_FR_QUERY_MAX);
    }
    snprintf(state->query_history[0], FRONTEND_FR_QUERY_MAX, "%s", text);
    state->query_history_index = 0u;
}

static const char *skip_ws(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static bool parse_u64_token(const char *text, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v;
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    if (text[0] == '$') {
        if (text[1] == '\0') {
            return false;
        }
        v = strtoull(text + 1, &end, 16);
    } else {
        v = strtoull(text, &end, 0);
    }
    if (end == text || (text[0] == '$' && end == text + 1) || *end != '\0') {
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static bool parse_u16_dec(const char *text, uint16_t *out)
{
    char *end = NULL;
    unsigned long v;
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    v = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || v > 65535ul) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

static bool token_eq_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool starts_with_ci(const char *text, const char *prefix)
{
    size_t i;
    if (text == NULL || prefix == NULL) {
        return false;
    }
    for (i = 0u; prefix[i] != '\0'; ++i) {
        if (tolower((unsigned char)text[i]) !=
            tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

static char *forensics_next_token(char **cursor)
{
    char *start;
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
    if (**cursor == '\0') {
        return NULL;
    }
    start = *cursor;
    while (**cursor != '\0' && !isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
    if (**cursor != '\0') {
        **cursor = '\0';
        (*cursor)++;
    }
    return start;
}

bool forensics_view_parse_query(
    const char *text,
    uint64_t last_cursor,
    frontend_forensics_query_parsed *out)
{
    char buf[FRONTEND_FR_QUERY_MAX];
    char *tok;
    const char *p;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->limit = 64u;
    out->from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
    p = skip_ws(text);
    if (p == NULL || *p == '\0') {
        out->ok = true;
        out->empty = true;
        return true;
    }
    snprintf(buf, sizeof(buf), "%s", p);
    snprintf(out->label, sizeof(out->label), "%s", p);

    if (starts_with_ci(buf, "info") &&
        (buf[4] == '\0' || isspace((unsigned char)buf[4]))) {
        out->ok = true;
        out->verb_code = FR_VERB_INFO;
        return true;
    }

    if (starts_with_ci(buf, "next") &&
        (buf[4] == '\0' || isspace((unsigned char)buf[4]))) {
        uint16_t limit = 64u;
        char *cursor = buf + 4;
        while ((tok = forensics_next_token(&cursor)) != NULL) {
            if (starts_with_ci(tok, "limit=")) {
                if (!parse_u16_dec(tok + 6, &limit) || limit == 0u ||
                    limit > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
                    snprintf(out->error, sizeof(out->error), "bad-args");
                    return false;
                }
            } else {
                snprintf(out->error, sizeof(out->error), "bad-args");
                return false;
            }
        }
        if (last_cursor == 0u) {
            snprintf(
                out->error,
                sizeof(out->error),
                "bad-args (no cursor — run find first)");
            return false;
        }
        out->ok = true;
        out->verb_code = FR_VERB_NEXT;
        out->limit = limit;
        return true;
    }

    if (starts_with_ci(buf, "read") &&
        (buf[4] == '\0' || isspace((unsigned char)buf[4]))) {
        uint64_t id = 0u;
        uint16_t before = 0u;
        uint16_t after = 0u;
        uint64_t epoch = 0u;
        bool have_id = false;
        char *cursor = buf + 4;
        while ((tok = forensics_next_token(&cursor)) != NULL) {
            if (starts_with_ci(tok, "before=")) {
                if (!parse_u16_dec(tok + 7, &before) ||
                    before > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
                    snprintf(out->error, sizeof(out->error), "bad-args");
                    return false;
                }
            } else if (starts_with_ci(tok, "after=")) {
                if (!parse_u16_dec(tok + 6, &after) ||
                    after > RUNTIME_HISTORY_MAX_QUERY_RECORDS) {
                    snprintf(out->error, sizeof(out->error), "bad-args");
                    return false;
                }
            } else if (starts_with_ci(tok, "epoch=")) {
                if (!parse_u64_token(tok + 6, &epoch)) {
                    snprintf(out->error, sizeof(out->error), "bad-args");
                    return false;
                }
            } else if (!have_id) {
                if (!parse_u64_token(tok, &id) || id == 0u) {
                    snprintf(out->error, sizeof(out->error), "bad-args");
                    return false;
                }
                have_id = true;
            } else {
                snprintf(out->error, sizeof(out->error), "bad-args");
                return false;
            }
        }
        if (!have_id) {
            snprintf(out->error, sizeof(out->error), "bad-args (read needs id)");
            return false;
        }
        out->ok = true;
        out->verb_code = FR_VERB_READ;
        out->read_id = id;
        out->read_epoch = epoch;
        out->before = before;
        out->after = after;
        return true;
    }

    {
        const char *options = buf;
        if (starts_with_ci(buf, "find") &&
            (buf[4] == '\0' || isspace((unsigned char)buf[4]))) {
            options = skip_ws(buf + 4);
        }
        if (!runtime_history_parse_find_options(
                options,
                &out->query,
                &out->from_kind,
                &out->from_id,
                &out->limit)) {
            snprintf(out->error, sizeof(out->error), "bad-args");
            return false;
        }
        out->ok = true;
        out->verb_code = FR_VERB_FIND;
        return true;
    }
}

/*
 * Complete from a NULL-terminated table.
 * One match → full string. Several → longest common prefix if it grows the
 * prefix (so "ac" → "access"); no progress → false.
 */
static bool complete_from_table(
    char *dst,
    size_t dst_cap,
    const char *prefix,
    const char *const *table,
    bool *out_unique)
{
    const char *matches[64];
    size_t n = 0u;
    size_t i;
    size_t prefix_len;
    size_t lcp;

    if (out_unique != NULL) {
        *out_unique = false;
    }
    if (dst == NULL || dst_cap == 0u || table == NULL || prefix == NULL) {
        return false;
    }
    prefix_len = strlen(prefix);
    for (i = 0u; table[i] != NULL; ++i) {
        if (starts_with_ci(table[i], prefix)) {
            if (n < sizeof(matches) / sizeof(matches[0])) {
                matches[n] = table[i];
            }
            ++n;
        }
    }
    if (n == 0u || n > sizeof(matches) / sizeof(matches[0])) {
        return false;
    }
    if (n == 1u) {
        if (out_unique != NULL) {
            *out_unique = true;
        }
        snprintf(dst, dst_cap, "%s", matches[0]);
        return true;
    }
    lcp = strlen(matches[0]);
    for (i = 1u; i < n; ++i) {
        size_t j = 0u;
        while (j < lcp && matches[0][j] != '\0' && matches[i][j] != '\0' &&
               tolower((unsigned char)matches[0][j]) ==
                   tolower((unsigned char)matches[i][j])) {
            ++j;
        }
        lcp = j;
    }
    if (lcp <= prefix_len) {
        return false;
    }
    if (lcp >= dst_cap) {
        lcp = dst_cap - 1u;
    }
    memcpy(dst, matches[0], lcp);
    dst[lcp] = '\0';
    return true;
}

bool forensics_view_autocomplete(frontend_forensics_state *state)
{
    char *start;
    char *eq;
    char prefix[FRONTEND_FR_QUERY_MAX];
    char completed[FRONTEND_FR_QUERY_MAX];
    size_t prefix_len;
    size_t head_len;
    bool unique = false;
    char before[FRONTEND_FR_QUERY_MAX];

    if (state == NULL) {
        return false;
    }
    snprintf(before, sizeof(before), "%s", state->query);
    start = state->query + (size_t)strlen(state->query);
    while (start > state->query && !isspace((unsigned char)start[-1])) {
        --start;
    }
    head_len = (size_t)(start - state->query);
    snprintf(prefix, sizeof(prefix), "%s", start);
    prefix_len = strlen(prefix);
    if (prefix_len == 0u) {
        forensics_view_set_status(
            state, "Tab: type a key prefix (e.g. acc → access=)");
        return false;
    }
    eq = strchr(prefix, '=');
    if (eq != NULL) {
        char key[64];
        size_t key_len = (size_t)(eq - prefix);
        const char *value = eq + 1;
        if (key_len >= sizeof(key)) {
            return false;
        }
        memcpy(key, prefix, key_len);
        key[key_len] = '\0';
        completed[0] = '\0';
        if (token_eq_ci(key, "access")) {
            if (!complete_from_table(
                    completed,
                    sizeof(completed),
                    value,
                    runtime_history_find_access_names(),
                    &unique)) {
                forensics_view_set_status(state, "Tab: no access-name match");
                return false;
            }
        } else if (token_eq_ci(key, "direction")) {
            static const char *const dirs[] = {"forward", "backward", NULL};
            if (!complete_from_table(
                    completed, sizeof(completed), value, dirs, &unique)) {
                forensics_view_set_status(state, "Tab: no direction match");
                return false;
            }
        } else if (token_eq_ci(key, "from")) {
            static const char *const froms[] = {"oldest", "newest", NULL};
            if (!complete_from_table(
                    completed, sizeof(completed), value, froms, &unique)) {
                forensics_view_set_status(state, "Tab: no from= match");
                return false;
            }
        } else {
            forensics_view_set_status(
                state, "Tab: value complete for access=/direction=/from=");
            return false;
        }
        if (head_len + key_len + 1u + strlen(completed) + 1u >=
            sizeof(state->query)) {
            return false;
        }
        snprintf(
            state->query + head_len,
            sizeof(state->query) - head_len,
            "%s=%s",
            key,
            completed);
    } else {
        if (!complete_from_table(
                completed,
                sizeof(completed),
                prefix,
                runtime_history_find_option_keys(),
                &unique)) {
            forensics_view_set_status(
                state, "Tab: no unique key (try acc, addr, lim, …)");
            return false;
        }
        if (head_len + strlen(completed) + (unique ? 2u : 1u) >=
            sizeof(state->query)) {
            return false;
        }
        if (unique) {
            snprintf(
                state->query + head_len,
                sizeof(state->query) - head_len,
                "%s=",
                completed);
        } else {
            snprintf(
                state->query + head_len,
                sizeof(state->query) - head_len,
                "%s",
                completed);
        }
    }
    if (strcmp(before, state->query) == 0) {
        return false;
    }
    state->query_rewrite_pending = true;
    state->query_focus_pending = true;
    return true;
}

/* --- HST1 formatters (match Ctl.format_hst1_record compact=True) --------- */

static const char *hst1_access_name(unsigned kind)
{
    static const char *const names[] = {
        "read",
        "write",
        "opcode",
        "operand",
        "dummy_read",
        "rmw_dummy_write",
        "stack_read",
        "stack_write",
        "vector_read"
    };
    if (kind < sizeof(names) / sizeof(names[0])) {
        return names[kind];
    }
    return NULL;
}

static const char *hst1_record_kind_name(unsigned kind)
{
    static const char *const names[] = {
        "exec", "marker", "reserved2", "reserved3"
    };
    if (kind < sizeof(names) / sizeof(names[0])) {
        return names[kind];
    }
    return NULL;
}

static int append_fmt(char *dst, size_t cap, size_t *used, const char *fmt, ...)
{
    va_list ap;
    int n;
    size_t remain;
    if (dst == NULL || used == NULL || cap == 0u || *used >= cap) {
        return -1;
    }
    remain = cap - *used;
    va_start(ap, fmt);
    n = vsnprintf(dst + *used, remain, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return -1;
    }
    if ((size_t)n >= remain) {
        *used = cap - 1u;
        return 1; /* truncated */
    }
    *used += (size_t)n;
    return 0;
}

size_t forensics_format_hst1_record(
    char *dst,
    size_t dst_cap,
    const runtime_history_record *record,
    bool anchor_match,
    bool compact)
{
    size_t used = 0u;
    char flags[64];
    size_t flag_used = 0u;
    unsigned i;
    int trunc = 0;
    const char *kind_name;

    if (dst == NULL || dst_cap == 0u || record == NULL) {
        return 0u;
    }
    dst[0] = '\0';
    flags[0] = '\0';
    if (record->partial) {
        (void)append_fmt(flags, sizeof(flags), &flag_used, "%s%s",
            flag_used ? "," : "", "partial");
    }
    if (record->access_truncated) {
        (void)append_fmt(flags, sizeof(flags), &flag_used, "%s%s",
            flag_used ? "," : "", "access_truncated");
    }
    if (anchor_match) {
        (void)append_fmt(flags, sizeof(flags), &flag_used, "%s%s",
            flag_used ? "," : "", "anchor");
    }
    if (record->timing_truncated) {
        (void)append_fmt(flags, sizeof(flags), &flag_used, "%s%s",
            flag_used ? "," : "", "timing_truncated");
    }

    kind_name = hst1_record_kind_name((unsigned)record->kind);
    if (record->kind != RUNTIME_HISTORY_RECORD_INSTRUCTION) {
        trunc |= append_fmt(
            dst,
            dst_cap,
            &used,
            "id=%llu kind=%s%s%s%s cyc=%llu marker=%u arg0=%u arg1=%u",
            (unsigned long long)record->id,
            kind_name != NULL ? kind_name : "kind?",
            flag_used ? " [" : "",
            flags,
            flag_used ? "]" : "",
            (unsigned long long)record->machine_cycle,
            (unsigned)record->marker_kind,
            (unsigned)record->marker_arg0,
            (unsigned)record->marker_arg1);
        if (trunc) {
            if (dst_cap >= 4u) {
                memcpy(dst + dst_cap - 4u, "...", 4u);
            }
        }
        return used;
    }

    trunc |= append_fmt(
        dst,
        dst_cap,
        &used,
        "id=%llu pc=$%04X a=%02X x=%02X y=%02X sp=%02X p=%02X opcode=$%02X "
        "cyc=%llu",
        (unsigned long long)record->id,
        (unsigned)record->pc,
        (unsigned)record->a,
        (unsigned)record->x,
        (unsigned)record->y,
        (unsigned)record->sp,
        (unsigned)record->p,
        (unsigned)record->opcode,
        (unsigned long long)record->machine_cycle);
    if (flag_used > 0u) {
        trunc |= append_fmt(dst, dst_cap, &used, " [%s]", flags);
    }

    {
        unsigned shown_idx[RUNTIME_HISTORY_MAX_MATERIALIZED_ACCESSES];
        unsigned shown_count = 0u;
        if (compact) {
            for (i = 0u; i < record->access_count; ++i) {
                if (record->accesses[i].kind == C6510_BUS_ACCESS_DATA_WRITE) {
                    shown_idx[shown_count++] = i;
                }
            }
            if (shown_count == 0u) {
                for (i = 0u; i < record->access_count; ++i) {
                    unsigned k = (unsigned)record->accesses[i].kind;
                    if (k != C6510_BUS_ACCESS_OPCODE_FETCH &&
                        k != C6510_BUS_ACCESS_OPERAND_READ) {
                        shown_idx[shown_count++] = i;
                    }
                }
            }
            if (shown_count == 0u) {
                for (i = 0u; i < record->access_count; ++i) {
                    shown_idx[shown_count++] = i;
                }
            }
        } else {
            for (i = 0u; i < record->access_count; ++i) {
                shown_idx[shown_count++] = i;
            }
        }
        if (shown_count > 0u) {
            trunc |= append_fmt(dst, dst_cap, &used, " accesses:");
            for (i = 0u; i < shown_count; ++i) {
                const runtime_history_access *a =
                    &record->accesses[shown_idx[i]];
                const char *an = hst1_access_name((unsigned)a->kind);
                char kindbuf[16];
                if (an == NULL) {
                    snprintf(kindbuf, sizeof(kindbuf), "kind%u",
                        (unsigned)a->kind);
                    an = kindbuf;
                }
                trunc |= append_fmt(
                    dst,
                    dst_cap,
                    &used,
                    "%s%s $%04X=%02X @+%u",
                    i == 0u ? " " : ", ",
                    an,
                    (unsigned)a->address,
                    (unsigned)a->value,
                    (unsigned)a->cycle_offset);
            }
        }
    }
    if (trunc) {
        if (dst_cap >= 4u) {
            memcpy(dst + dst_cap - 4u, "...", 4u);
            used = dst_cap - 1u;
        }
    }
    return used;
}

static void forensics_rebuild_display(frontend_forensics_state *state)
{
    unsigned li;
    unsigned display_cap =
        (unsigned)(sizeof(state->display_logical_index) /
                   sizeof(state->display_logical_index[0]));

    if (state == NULL) {
        return;
    }
    state->display_count = 0u;
    for (li = 0u; li < state->logical_count; ++li) {
        frontend_fr_logical_entry *entry = &state->logical[li];
        const char *text = entry->text != NULL ? entry->text : "";
        size_t len = strlen(text);
        size_t off = 0u;

        entry->display_begin = state->display_count;
        entry->display_count = 0u;
        if (len == 0u) {
            if (state->display_count >= display_cap) {
                break;
            }
            state->display_logical_index[state->display_count] = li;
            state->display_off[state->display_count] = 0u;
            state->display_len[state->display_count] = 0u;
            state->display_count++;
            entry->display_count = 1u;
            continue;
        }
        while (off < len && state->display_count < display_cap) {
            size_t remain = len - off;
            size_t take = remain;
            size_t j;

            if (take > FRONTEND_FR_DISPLAY_COLS) {
                take = FRONTEND_FR_DISPLAY_COLS;
                /* Prefer break after ", " within the window. */
                for (j = take; j > 0u; --j) {
                    if (off + j >= 2u && text[off + j - 2u] == ',' &&
                        text[off + j - 1u] == ' ') {
                        take = j;
                        break;
                    }
                }
            }
            state->display_logical_index[state->display_count] = li;
            state->display_off[state->display_count] = (unsigned)off;
            state->display_len[state->display_count] = (unsigned)take;
            state->display_count++;
            entry->display_count++;
            off += take;
        }
    }
}

static bool forensics_drop_oldest(frontend_forensics_state *state)
{
    unsigned i;
    if (state == NULL || state->logical_count == 0u) {
        return false;
    }
    free(state->logical[0].text);
    for (i = 1u; i < state->logical_count; ++i) {
        state->logical[i - 1u] = state->logical[i];
    }
    state->logical_count--;
    state->logical[state->logical_count].text = NULL;
    if (state->sel_logical_first != FORENSICS_SEL_NONE) {
        if (state->sel_logical_first == 0u) {
            state->sel_logical_first = FORENSICS_SEL_NONE;
            state->sel_logical_last = FORENSICS_SEL_NONE;
            state->has_land_selection = false;
        } else {
            state->sel_logical_first--;
            if (state->sel_logical_last != FORENSICS_SEL_NONE &&
                state->sel_logical_last > 0u) {
                state->sel_logical_last--;
            }
        }
    }
    return true;
}

static bool forensics_append_logical(
    frontend_forensics_state *state,
    const char *text,
    bool is_record,
    bool is_header,
    uint64_t id,
    uint64_t cycle,
    bool has_cycle)
{
    frontend_fr_logical_entry *entry;
    char *dup;
    size_t n;

    if (state == NULL) {
        return false;
    }
    while (state->logical_count >= FRONTEND_FR_LOGICAL_CAP) {
        if (!forensics_drop_oldest(state)) {
            return false;
        }
    }
    n = text != NULL ? strlen(text) : 0u;
    dup = (char *)malloc(n + 1u);
    if (dup == NULL) {
        return false;
    }
    if (text != NULL) {
        memcpy(dup, text, n + 1u);
    } else {
        dup[0] = '\0';
    }
    entry = &state->logical[state->logical_count];
    memset(entry, 0, sizeof(*entry));
    entry->text = dup;
    entry->is_record = is_record;
    entry->is_header = is_header;
    entry->id = id;
    entry->cycle = cycle;
    entry->has_cycle = has_cycle;
    state->logical_count++;
    return true;
}

static void forensics_update_land_from_selection(frontend_forensics_state *state)
{
    unsigned i;
    if (state == NULL) {
        return;
    }
    state->has_land_selection = false;
    state->selected_cycle = 0u;
    state->selected_id = 0u;
    if (state->sel_logical_first == FORENSICS_SEL_NONE ||
        state->sel_logical_first >= state->logical_count) {
        return;
    }
    for (i = state->sel_logical_first;
         i <= state->sel_logical_last && i < state->logical_count;
         ++i) {
        const frontend_fr_logical_entry *e = &state->logical[i];
        if (e->is_record && e->has_cycle) {
            state->has_land_selection = true;
            state->selected_cycle = e->cycle;
            state->selected_id = e->id;
            return;
        }
    }
}

static void forensics_select_logical(
    frontend_forensics_state *state,
    unsigned li)
{
    unsigned last;

    if (state == NULL || li >= state->logical_count) {
        return;
    }
    if (state->logical[li].is_header) {
        last = li;
        while (last + 1u < state->logical_count &&
               !state->logical[last + 1u].is_header) {
            last++;
        }
        state->sel_logical_first = li;
        state->sel_logical_last = last;
    } else {
        state->sel_logical_first = li;
        state->sel_logical_last = li;
    }
    forensics_update_land_from_selection(state);
}

static void forensics_copy_selection(frontend_forensics_state *state)
{
    unsigned i;
    size_t total = 0u;
    char *buf;
    size_t used = 0u;

    if (state == NULL) {
        return;
    }
    if (state->sel_logical_first == FORENSICS_SEL_NONE ||
        state->sel_logical_first >= state->logical_count) {
        /* Copy last result block if nothing selected. */
        unsigned start = FORENSICS_SEL_NONE;
        for (i = state->logical_count; i > 0u; --i) {
            if (state->logical[i - 1u].is_header) {
                start = i - 1u;
                break;
            }
        }
        if (start == FORENSICS_SEL_NONE) {
            forensics_view_set_status(state, "nothing to copy");
            return;
        }
        state->sel_logical_first = start;
        state->sel_logical_last = state->logical_count - 1u;
        forensics_update_land_from_selection(state);
    }
    for (i = state->sel_logical_first;
         i <= state->sel_logical_last && i < state->logical_count;
         ++i) {
        const char *t = state->logical[i].text;
        total += (t != NULL ? strlen(t) : 0u) + 1u;
    }
    if (total == 0u) {
        forensics_view_set_status(state, "nothing to copy");
        return;
    }
    buf = (char *)malloc(total + 1u);
    if (buf == NULL) {
        forensics_view_set_status(state, "copy failed");
        return;
    }
    for (i = state->sel_logical_first;
         i <= state->sel_logical_last && i < state->logical_count;
         ++i) {
        const char *t = state->logical[i].text;
        size_t n = t != NULL ? strlen(t) : 0u;
        if (used > 0u) {
            buf[used++] = '\n';
        }
        if (n > 0u) {
            memcpy(buf + used, t, n);
            used += n;
        }
    }
    buf[used] = '\0';
    if (SDL_SetClipboardText(buf) == 0) {
        forensics_view_set_status(state, "copied selection");
    } else {
        forensics_view_set_status(state, "copy failed");
    }
    free(buf);
}

bool forensics_token_at_offset(
    const char *text,
    size_t byte_offset,
    char *out,
    size_t out_cap)
{
    static const char *const keys[] = {"id=", "cyc=", "pc=$", NULL};
    size_t i;
    size_t len;

    if (out != NULL && out_cap > 0u) {
        out[0] = '\0';
    }
    if (text == NULL || out == NULL || out_cap == 0u) {
        return false;
    }
    len = strlen(text);
    if (byte_offset > len) {
        byte_offset = len;
    }
    if (byte_offset == len && len > 0u) {
        byte_offset = len - 1u;
    }
    for (i = 0u; keys[i] != NULL; ++i) {
        const char *key = keys[i];
        size_t key_len = strlen(key);
        const char *p = text;
        while ((p = strstr(p, key)) != NULL) {
            size_t start = (size_t)(p - text);
            size_t end = start + key_len;
            if (strcmp(key, "pc=$") == 0) {
                while (end < len && isxdigit((unsigned char)text[end])) {
                    end++;
                }
            } else {
                while (end < len && isdigit((unsigned char)text[end])) {
                    end++;
                }
            }
            if (end > start + key_len &&
                byte_offset >= start && byte_offset < end) {
                size_t n = end - start;
                if (n + 1u > out_cap) {
                    n = out_cap - 1u;
                }
                memcpy(out, text + start, n);
                out[n] = '\0';
                return true;
            }
            p += key_len;
        }
    }
    return false;
}

static size_t forensics_char_index_at_x(
    const struct nk_user_font *font,
    const char *line,
    size_t line_len,
    float x)
{
    size_t i;
    float acc = 0.0f;

    if (font == NULL || line == NULL || line_len == 0u || x <= 0.0f) {
        return 0u;
    }
    for (i = 0u; i < line_len; ++i) {
        float w = font->width(font->userdata, font->height, line + i, 1);
        if (x < acc + w) {
            return i;
        }
        acc += w;
    }
    return line_len > 0u ? line_len - 1u : 0u;
}

static bool forensics_copy_token_at_row(
    struct nk_context *ctx,
    frontend_forensics_state *state,
    unsigned display_index,
    struct nk_rect row_bounds)
{
    unsigned li;
    const frontend_fr_logical_entry *entry;
    char line[FRONTEND_FR_DISPLAY_COLS + 4u];
    char token[64];
    unsigned off;
    unsigned len;
    size_t char_index;
    size_t byte_offset;
    float x;

    if (ctx == NULL || state == NULL || display_index >= state->display_count) {
        return false;
    }
    li = state->display_logical_index[display_index];
    if (li >= state->logical_count) {
        return false;
    }
    entry = &state->logical[li];
    if (entry->text == NULL || !entry->is_record) {
        return false;
    }
    off = state->display_off[display_index];
    len = state->display_len[display_index];
    if (off > strlen(entry->text)) {
        return false;
    }
    if (len > FRONTEND_FR_DISPLAY_COLS) {
        len = FRONTEND_FR_DISPLAY_COLS;
    }
    if (off + len > strlen(entry->text)) {
        len = (unsigned)strlen(entry->text) - off;
    }
    memcpy(line, entry->text + off, len);
    line[len] = '\0';
    x = ctx->input.mouse.pos.x - row_bounds.x;
    char_index = forensics_char_index_at_x(ctx->style.font, line, len, x);
    byte_offset = (size_t)off + char_index;
    if (!forensics_token_at_offset(entry->text, byte_offset, token, sizeof(token))) {
        return false;
    }
    if (SDL_SetClipboardText(token) != 0) {
        forensics_view_set_status(state, "copy failed");
        return true;
    }
    {
        char status[FRONTEND_FR_STATUS_MAX];
        snprintf(status, sizeof(status), "copied %s", token);
        forensics_view_set_status(state, status);
    }
    return true;
}

void forensics_view_apply_rpc_error(
    frontend_forensics_state *state,
    runtime_history_rpc_status status)
{
    const char *msg = "history-query-failed";
    switch (status) {
    case RUNTIME_HISTORY_RPC_UNAVAILABLE:
        msg = "history-recorder-unavailable";
        break;
    case RUNTIME_HISTORY_RPC_MACHINE_RUNNING:
        msg = "machine-running";
        break;
    case RUNTIME_HISTORY_RPC_REQUEST_ACTIVE:
        msg = "history-request-active";
        break;
    case RUNTIME_HISTORY_RPC_BAD_ARGS:
        msg = "history-query-invalid";
        break;
    case RUNTIME_HISTORY_RPC_CURSOR_STALE:
        msg = "history-cursor-stale";
        break;
    case RUNTIME_HISTORY_RPC_EPOCH_MISMATCH:
        msg = "history-epoch-mismatch";
        break;
    case RUNTIME_HISTORY_RPC_RECORD_NOT_RETAINED:
        msg = "history-record-not-retained";
        break;
    default:
        break;
    }
    forensics_view_set_status(state, msg);
}

void forensics_view_apply_land_focus(
    frontend_forensics_state *state,
    const frontend_forensics_land_context *land)
{
    char text[FRONTEND_FR_STATUS_MAX];
    uint64_t requested;
    uint64_t focus;
    bool exact;

    if (state == NULL || land == NULL || !state->land_awaiting_focus) {
        return;
    }
    if (!land->inspecting) {
        return;
    }
    requested = state->land_requested_cycle;
    focus = land->focus_cycle;
    exact = state->land_awaiting_exact;
    state->land_awaiting_focus = false;
    state->land_awaiting_exact = false;
    if (focus == requested) {
        snprintf(
            text,
            sizeof(text),
            exact ? "landed exact focus_cycle=%llu" :
                    "landed focus_cycle=%llu",
            (unsigned long long)focus);
    } else if (land->newest_cycle > 0u && focus >= land->newest_cycle) {
        snprintf(
            text,
            sizeof(text),
            "landed focus_cycle=%llu (requested %llu → live)",
            (unsigned long long)focus,
            (unsigned long long)requested);
    } else if (land->oldest_cycle > 0u && requested < land->oldest_cycle) {
        snprintf(
            text,
            sizeof(text),
            "landed focus_cycle=%llu (requested %llu → clamped oldest)",
            (unsigned long long)focus,
            (unsigned long long)requested);
    } else if (exact) {
        snprintf(
            text,
            sizeof(text),
            "landed focus_cycle=%llu (requested %llu, partial exact)",
            (unsigned long long)focus,
            (unsigned long long)requested);
    } else {
        snprintf(
            text,
            sizeof(text),
            "landed focus_cycle=%llu (requested %llu, before/quantized)",
            (unsigned long long)focus,
            (unsigned long long)requested);
    }
    forensics_view_set_status(state, text);
    /* Successful Inspect focus update → leave to debugger + Inspector tab. */
    state->request_leave_debugger = true;
}

static void forensics_try_land_button(
    frontend_forensics_state *state,
    const frontend_forensics_land_context *land,
    bool exact)
{
    if (state == NULL || !state->has_land_selection) {
        return;
    }
    if (land == NULL || !land->window_valid) {
        forensics_view_set_status(state, "cannot land — no checkpoints");
        return;
    }
    state->pending_land_cycle = state->selected_cycle;
    state->land_confirm_exact = exact;
    if (land->inspecting) {
        state->request_land = true;
        state->request_land_exact = exact;
        state->request_land_enter = false;
        forensics_view_set_status(
            state, exact ? "landing exact…" : "landing before…");
        return;
    }
    if (land->can_enter) {
        state->land_confirm_open = true;
        return;
    }
    forensics_view_set_status(state, "cannot land — no checkpoints");
}

static void forensics_draw_land_confirm(
    struct nk_context *ctx,
    frontend_forensics_state *state)
{
    char line[192];
    bool exact;

    if (ctx == NULL || state == NULL || !state->land_confirm_open) {
        return;
    }
    exact = state->land_confirm_exact;
    snprintf(
        line,
        sizeof(line),
        exact ? "Enter Inspect and land exactly at cycle %llu?" :
                "Enter Inspect and land before cycle %llu (checkpoint ≤ N)?",
        (unsigned long long)state->pending_land_cycle);
    if (nk_popup_begin(
            ctx,
            NK_POPUP_STATIC,
            exact ? "Inspect & Land exact" : "Inspect & Land before",
            NK_WINDOW_BORDER | NK_WINDOW_TITLE,
            nk_rect(160.0f, 160.0f, 460.0f, 120.0f))) {
        nk_layout_row_dynamic(ctx, 22.0f, 1);
        nk_label_wrap(ctx, line);
        nk_layout_row_dynamic(ctx, 26.0f, 2);
        if (nk_button_label(ctx, exact ? "Inspect & Land exact" : "Inspect & Land before")) {
            state->land_confirm_open = false;
            state->request_land = true;
            state->request_land_exact = exact;
            state->request_land_enter = true;
            forensics_view_set_status(
                state,
                exact ? "entering Inspect & landing exact…" :
                        "entering Inspect & landing before…");
            nk_popup_close(ctx);
        }
        if (nk_button_label(ctx, "Cancel")) {
            state->land_confirm_open = false;
            state->land_confirm_exact = false;
            forensics_view_set_status(state, "land canceled");
            nk_popup_close(ctx);
        }
        nk_popup_end(ctx);
    } else {
        state->land_confirm_open = false;
    }
}

void forensics_view_apply_status(
    frontend_forensics_state *state,
    const runtime_history_status *status,
    bool append_transcript_note)
{
    char text[FRONTEND_FR_STATUS_MAX];
    if (state == NULL || status == NULL) {
        return;
    }
    if (!status->available) {
        snprintf(
            text,
            sizeof(text),
            "available=0 recording=0 requested_bytes=%llu capacity_bytes=0",
            (unsigned long long)status->requested_bytes);
    } else {
        snprintf(
            text,
            sizeof(text),
            "recording=%u epoch=%llu used=%llu/%llu oldest=%llu newest=%llu",
            status->recording ? 1u : 0u,
            (unsigned long long)status->epoch,
            (unsigned long long)status->used_bytes,
            (unsigned long long)status->capacity_bytes,
            (unsigned long long)status->oldest_id,
            (unsigned long long)status->newest_id);
        state->last_epoch = status->epoch;
    }
    forensics_view_set_status(state, text);
    if (append_transcript_note) {
        (void)forensics_append_logical(
            state, "--- info ---", false, true, 0u, 0u, false);
        (void)forensics_append_logical(
            state, text, false, false, 0u, 0u, false);
        forensics_rebuild_display(state);
    }
}

void forensics_view_apply_result(
    frontend_forensics_state *state,
    int verb_code,
    const char *label,
    const runtime_history_rpc_meta *meta,
    const runtime_history_record *records,
    size_t record_count,
    const bool *anchor_matches)
{
    char header[FRONTEND_FR_FORMAT_CAP];
    char meta_line[FRONTEND_FR_FORMAT_CAP];
    char line[FRONTEND_FR_FORMAT_CAP];
    const char *verb_name = "find";
    size_t i;

    if (state == NULL || meta == NULL) {
        return;
    }
    switch (verb_code) {
    case FR_VERB_NEXT:
        verb_name = "next";
        break;
    case FR_VERB_READ:
        verb_name = "read";
        break;
    case FR_VERB_INFO:
        verb_name = "info";
        break;
    case FR_VERB_CLOSE:
        forensics_view_set_status(state, "cursor closed");
        return;
    default:
        verb_name = "find";
        break;
    }

    state->last_cursor = meta->cursor;
    state->last_more = meta->more != 0u;
    state->last_epoch = meta->epoch;

    if (label != NULL && label[0] != '\0') {
        snprintf(header, sizeof(header), "--- %s ---", label);
    } else {
        snprintf(header, sizeof(header), "--- %s ---", verb_name);
    }
    (void)forensics_append_logical(state, "", false, false, 0u, 0u, false);
    (void)forensics_append_logical(state, header, false, true, 0u, 0u, false);

    snprintf(
        meta_line,
        sizeof(meta_line),
        "epoch=%llu count=%u cursor=%llu more=%u oldest=%llu newest=%llu",
        (unsigned long long)meta->epoch,
        meta->count,
        (unsigned long long)meta->cursor,
        meta->more ? 1u : 0u,
        (unsigned long long)meta->oldest,
        (unsigned long long)meta->newest);
    (void)forensics_append_logical(
        state, meta_line, false, false, 0u, 0u, false);

    if (records == NULL || record_count == 0u) {
        (void)forensics_append_logical(
            state, "(no records)", false, false, 0u, 0u, false);
    } else {
        for (i = 0u; i < record_count; ++i) {
            bool anchor =
                anchor_matches != NULL ? anchor_matches[i] : false;
            size_t n = forensics_format_hst1_record(
                line, sizeof(line), &records[i], anchor, true);
            if (n + 1u >= sizeof(line) ||
                (sizeof(line) >= 4u &&
                 strcmp(line + sizeof(line) - 4u, "...") == 0)) {
                /* Detect truncation via trailing ... or full buffer. */
                if (n >= sizeof(line) - 1u) {
                    state->line_truncated = true;
                }
            }
            if (n >= FRONTEND_FR_FORMAT_CAP - 1u) {
                state->line_truncated = true;
            }
            (void)forensics_append_logical(
                state,
                line,
                true,
                false,
                records[i].id,
                records[i].machine_cycle,
                true);
        }
    }

    forensics_rebuild_display(state);
    {
        char strip[FRONTEND_FR_STATUS_MAX];
        snprintf(
            strip,
            sizeof(strip),
            "epoch=%llu count=%u cursor=%llu more=%u%s",
            (unsigned long long)meta->epoch,
            meta->count,
            (unsigned long long)meta->cursor,
            meta->more ? 1u : 0u,
            state->line_truncated ? " line-truncated" : "");
        forensics_view_set_status(state, strip);
    }
}

void forensics_view_render(
    struct nk_context *ctx,
    frontend_forensics_state *state,
    int width,
    int height,
    const frontend_forensics_land_context *land)
{
    struct nk_rect bounds;
    struct nk_style_window saved_window;
    struct nk_style_edit saved_edit;
    /* Single surface color for window, transcript group, and chrome. */
    const struct nk_color fr_bg = nk_rgb(28, 28, 34);
    float toolbar_h = 28.0f;
    float hint_h = 18.0f;
    float status_h = 20.0f;
    float query_h = 26.0f;
    float title_and_pad = 44.0f;
    float row_h = 16.0f;
    float content_h;
    unsigned i;

    if (ctx == NULL || state == NULL || !state->open || width <= 0 || height <= 0) {
        return;
    }

    bounds = nk_rect(0.0f, 0.0f, (float)width, (float)height);
    content_h = bounds.h - title_and_pad - toolbar_h - hint_h - status_h -
        query_h - 16.0f;
    if (content_h < 48.0f) {
        content_h = 48.0f;
    }

    saved_window = ctx->style.window;
    saved_edit = ctx->style.edit;
    /*
     * fixed_background paints the window/group body; background paints label
     * glyph backs and panel empty space (default NK_COLOR_WINDOW 45,45,45).
     * Both must match or status/Query and row text band against the panel.
     */
    ctx->style.window.background = fr_bg;
    ctx->style.window.fixed_background = nk_style_item_color(fr_bg);
    ctx->style.window.padding = nk_vec2(10.0f, 8.0f);
    ctx->style.window.spacing = nk_vec2(6.0f, 4.0f);
    ctx->style.window.group_padding = nk_vec2(6.0f, 4.0f);
    ctx->style.edit.normal = nk_style_item_color(fr_bg);
    ctx->style.edit.hover = nk_style_item_color(fr_bg);
    ctx->style.edit.active = nk_style_item_color(fr_bg);
    ctx->style.edit.border_color = nk_rgb(60, 60, 70);
    ctx->style.edit.text_normal = nk_rgb(200, 200, 205);
    ctx->style.edit.text_hover = nk_rgb(220, 220, 225);
    ctx->style.edit.text_active = nk_rgb(220, 220, 225);

    if (nk_begin(
            ctx,
            "Forensics",
            bounds,
            NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_begin(ctx, NK_STATIC, toolbar_h, 5);
        nk_layout_row_push(ctx, 70.0f);
        if (nk_button_label(ctx, "Close")) {
            state->request_close = true;
        }
        nk_layout_row_push(ctx, 90.0f);
        if (nk_button_label(ctx, "Clear view")) {
            forensics_view_clear_transcript(state);
        }
        nk_layout_row_push(ctx, 70.0f);
        if (nk_button_label(ctx, "Copy")) {
            forensics_copy_selection(state);
        }
        nk_layout_row_push(ctx, 110.0f);
        if (state->has_land_selection) {
            if (nk_button_label(ctx, "Land before")) {
                forensics_try_land_button(state, land, false);
            }
        } else {
            nk_widget_disable_begin(ctx);
            (void)nk_button_label(ctx, "Land before");
            nk_widget_disable_end(ctx);
        }
        nk_layout_row_push(ctx, 100.0f);
        if (state->has_land_selection) {
            if (nk_button_label(ctx, "Land exact")) {
                forensics_try_land_button(state, land, true);
            }
        } else {
            nk_widget_disable_begin(ctx);
            (void)nk_button_label(ctx, "Land exact");
            nk_widget_disable_end(ctx);
        }
        nk_layout_row_end(ctx);

        nk_layout_row_dynamic(ctx, hint_h, 1);
        nk_label(
            ctx,
            "Opt+R/Close return to entry. Land/F9 -> debugger (paused). Tab completes keys. Double-click id=/cyc=/pc=$ to copy.",
            NK_TEXT_LEFT);

        ctx->style.window.background = fr_bg;
        ctx->style.window.fixed_background = nk_style_item_color(fr_bg);
        nk_layout_row_dynamic(ctx, content_h, 1);
        if (nk_group_begin(ctx, "ForensicsTranscript", 0)) {
            if (state->display_count == 0u && state->logical_count == 0u) {
                nk_layout_row_dynamic(ctx, 18.0f, 1);
                nk_label_colored(
                    ctx,
                    "(empty transcript)",
                    NK_TEXT_LEFT,
                    nk_rgb(140, 140, 150));
            } else {
                for (i = 0u; i < state->display_count; ++i) {
                    unsigned li = state->display_logical_index[i];
                    const frontend_fr_logical_entry *entry;
                    char line[FRONTEND_FR_DISPLAY_COLS + 4u];
                    unsigned off;
                    unsigned len;
                    bool selected;
                    struct nk_rect row_bounds;

                    if (li >= state->logical_count) {
                        continue;
                    }
                    entry = &state->logical[li];
                    if (entry->text == NULL) {
                        continue;
                    }
                    off = state->display_off[i];
                    len = state->display_len[i];
                    if (off > strlen(entry->text)) {
                        continue;
                    }
                    if (len > FRONTEND_FR_DISPLAY_COLS) {
                        len = FRONTEND_FR_DISPLAY_COLS;
                    }
                    if (off + len > strlen(entry->text)) {
                        len = (unsigned)strlen(entry->text) - off;
                    }
                    memcpy(line, entry->text + off, len);
                    line[len] = '\0';

                    /*
                     * Bounds BEFORE the widget (disasm pattern).
                     * nk_widget_is_mouse_clicked peeks the *next* row — that
                     * was the off-by-one (click selected the line above).
                     */
                    nk_layout_row_dynamic(ctx, row_h, 1);
                    row_bounds = nk_widget_bounds(ctx);
                    if (nk_input_is_mouse_click_down_in_rect(
                            &ctx->input, NK_BUTTON_DOUBLE, row_bounds, nk_true)) {
                        (void)forensics_copy_token_at_row(
                            ctx, state, i, row_bounds);
                    } else if (
                        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT) &&
                        nk_input_has_mouse_click_in_rect(
                            &ctx->input, NK_BUTTON_LEFT, row_bounds)) {
                        forensics_select_logical(state, li);
                    }
                    selected =
                        state->sel_logical_first != FORENSICS_SEL_NONE &&
                        li >= state->sel_logical_first &&
                        li <= state->sel_logical_last;
                    if (selected) {
                        nk_label_colored(
                            ctx, line, NK_TEXT_LEFT, nk_rgb(220, 220, 120));
                    } else if (entry->is_header) {
                        nk_label_colored(
                            ctx, line, NK_TEXT_LEFT, nk_rgb(160, 180, 220));
                    } else {
                        nk_label(ctx, line, NK_TEXT_LEFT);
                    }
                }
            }
            nk_group_end(ctx);
        }

        ctx->style.window.background = fr_bg;
        ctx->style.window.fixed_background = nk_style_item_color(fr_bg);
        nk_layout_row_dynamic(ctx, status_h, 1);
        nk_label(ctx, state->status, NK_TEXT_LEFT);

        nk_layout_row_begin(ctx, NK_DYNAMIC, query_h, 2);
        nk_layout_row_push(ctx, 0.10f);
        nk_label(ctx, "Query", NK_TEXT_LEFT);
        nk_layout_row_push(ctx, 0.90f);
        {
            nk_flags edit_flags =
                NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE;
            nk_flags result;
            if (state->query_rewrite_pending) {
                /* Drop active edit so the next focus reads the new buffer. */
                nk_edit_unfocus(ctx);
                state->query_rewrite_pending = false;
                state->query_focus_pending = true;
            }
            if (state->query_focus_pending) {
                int end;
                nk_edit_focus(ctx, NK_EDIT_GOTO_END_ON_ACTIVATE);
                state->query_focus_pending = false;
                /* nk_edit_focus ignores GOTO_END; place caret after rewrite. */
                end = (int)strlen(state->query);
                ctx->current->edit.cursor = end;
                ctx->current->edit.sel_start = end;
                ctx->current->edit.sel_end = end;
            }
            result = nk_edit_string_zero_terminated(
                ctx,
                edit_flags,
                state->query,
                (int)sizeof(state->query),
                nk_filter_default);
            if (result & NK_EDIT_COMMITED) {
                state->request_submit = true;
            }
        }
        nk_layout_row_end(ctx);

        forensics_draw_land_confirm(ctx, state);
    }
    nk_end(ctx);
    ctx->style.window = saved_window;
    ctx->style.edit = saved_edit;
}

#pragma once

#include "nuklear_config.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_history_query_parse.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FRONTEND_FR_LOGICAL_CAP = 1024,
    FRONTEND_FR_DISPLAY_COLS = 160,
    FRONTEND_FR_FORMAT_CAP = 4096,
    FRONTEND_FR_QUERY_MAX = 256,
    FRONTEND_FR_QUERY_HISTORY = 64,
    FRONTEND_FR_STATUS_MAX = 192,
    FRONTEND_FR_LABEL_MAX = 160
};

typedef enum frontend_forensics_entry {
    FRONTEND_FORENSICS_ENTRY_DEBUGGER = 0,
    FRONTEND_FORENSICS_ENTRY_CRT = 1
} frontend_forensics_entry;

typedef struct frontend_fr_logical_entry {
    char *text; /* full formatter output; heap; never 256-capped */
    uint64_t cycle;
    uint64_t id;
    bool has_cycle;
    bool is_record; /* vs header / metadata / blank */
    bool is_header; /* block header (--- find … ---) */
    unsigned display_begin;
    unsigned display_count;
} frontend_fr_logical_entry;

typedef struct frontend_forensics_state {
    bool open;
    frontend_forensics_entry entry; /* where Opt+R/Close returns */
    bool crt_was_running; /* valid when entry == CRT: restore on Opt+R/Close */
    bool query_focus_pending;
    char query[FRONTEND_FR_QUERY_MAX];
    char query_history[FRONTEND_FR_QUERY_HISTORY][FRONTEND_FR_QUERY_MAX];
    unsigned query_history_count;
    unsigned query_history_index; /* 0 = live edit; 1..count browse older */
    frontend_fr_logical_entry logical[FRONTEND_FR_LOGICAL_CAP];
    unsigned logical_count;
    unsigned display_logical_index[FRONTEND_FR_LOGICAL_CAP * 2u];
    unsigned display_off[FRONTEND_FR_LOGICAL_CAP * 2u];
    unsigned display_len[FRONTEND_FR_LOGICAL_CAP * 2u];
    unsigned display_count;
    unsigned sel_logical_first; /* inclusive, or UINT_MAX */
    unsigned sel_logical_last;
    uint64_t selected_cycle;
    uint64_t selected_id;
    bool has_land_selection;
    uint64_t last_cursor;
    bool last_more;
    uint64_t last_epoch;
    char status[FRONTEND_FR_STATUS_MAX];
    bool request_close; /* Close button → leave to entry surface */
    bool request_host_pause; /* set on open; main pauses if still running */
    bool request_submit; /* Query Enter → frontend parses + pushes intent */
    bool line_truncated; /* last format hit FORMAT_CAP */
    bool query_rewrite_pending; /* Tab autocomplete rewrote query; re-focus edit */
    /* Land Inspector: quantized (before) and exact (PR6). */
    bool land_confirm_open; /* Inspect & Land popup */
    bool land_confirm_exact; /* confirm is for Land exact */
    bool request_land; /* flush: push land (and ENTER if request_land_enter) */
    bool request_land_exact; /* LAND_TO_CYCLE vs quantized LAND */
    bool request_land_enter; /* ENTER before land */
    uint64_t pending_land_cycle;
    bool land_awaiting_focus; /* wait for post-land inspector_focus_cycle */
    bool land_awaiting_exact; /* status wording: exact vs quantized */
    uint64_t land_requested_cycle;
} frontend_forensics_state;

/* Inspector gates for Land (from frontend_debug_state). */
typedef struct frontend_forensics_land_context {
    bool inspecting;
    bool window_valid;
    bool can_enter; /* enabled && window_valid */
    uint64_t focus_cycle;
    uint64_t oldest_cycle;
    uint64_t newest_cycle;
} frontend_forensics_land_context;

/* Result of leaving Forensics (main applies ui_visible + run/pause). */
typedef struct frontend_forensics_leave_result {
    bool show_debugger; /* true → debugger; false → full-screen CRT */
    bool resume_machine; /* true only when returning to CRT that was running */
} frontend_forensics_leave_result;

/* Parsed query-line → structured HISTORY fields (no RPC).
   verb_code is FRONTEND_HISTORY_VERB_* (defined in frontend.h). */
typedef struct frontend_forensics_query_parsed {
    bool ok;
    bool empty; /* blank Enter */
    int verb_code;
    runtime_history_query query;
    runtime_history_from_kind from_kind;
    uint64_t from_id;
    uint16_t limit;
    uint64_t read_id;
    uint64_t read_epoch;
    uint16_t before;
    uint16_t after;
    char label[FRONTEND_FR_LABEL_MAX];
    char error[FRONTEND_FR_STATUS_MAX];
} frontend_forensics_query_parsed;

void forensics_view_init(frontend_forensics_state *state);
void forensics_view_open(
    frontend_forensics_state *state,
    frontend_forensics_entry entry,
    bool crt_was_running);
void forensics_view_close(frontend_forensics_state *state);
bool forensics_view_is_open(const frontend_forensics_state *state);
frontend_forensics_entry forensics_view_entry(const frontend_forensics_state *state);
bool forensics_view_crt_was_running(const frontend_forensics_state *state);

/* Opt+R / Close: return to entry surface (may resume CRT). */
frontend_forensics_leave_result forensics_view_leave_to_entry(
    frontend_forensics_state *state);
/* F9: always debugger, always paused. */
frontend_forensics_leave_result forensics_view_leave_to_debugger(
    frontend_forensics_state *state);

void forensics_view_clear_transcript(frontend_forensics_state *state);
void forensics_view_set_status(frontend_forensics_state *state, const char *text);

/* Query-history browse (Up/Down). Returns true if query buffer changed. */
bool forensics_view_query_history_prev(frontend_forensics_state *state);
bool forensics_view_query_history_next(frontend_forensics_state *state);
void forensics_view_query_history_push(frontend_forensics_state *state, const char *text);

/* Tab autocomplete from shared find key/access tables. */
bool forensics_view_autocomplete(frontend_forensics_state *state);

/* Parse query line (find/next/read/info). Uses shared find-option parser. */
bool forensics_view_parse_query(
    const char *text,
    uint64_t last_cursor,
    frontend_forensics_query_parsed *out);

/* Compact HST1 line (north star: Ctl.format_hst1_record compact=True). */
size_t forensics_format_hst1_record(
    char *dst,
    size_t dst_cap,
    const runtime_history_record *record,
    bool anchor_match,
    bool compact);

/* Token under byte offset in a formatted line: id= / cyc= / pc=$… */
bool forensics_token_at_offset(
    const char *text,
    size_t byte_offset,
    char *out,
    size_t out_cap);

void forensics_view_apply_result(
    frontend_forensics_state *state,
    int verb_code,
    const char *label,
    const runtime_history_rpc_meta *meta,
    const runtime_history_record *records,
    size_t record_count,
    const bool *anchor_matches);
void forensics_view_apply_status(
    frontend_forensics_state *state,
    const runtime_history_status *status,
    bool append_transcript_note);
void forensics_view_apply_rpc_error(
    frontend_forensics_state *state,
    runtime_history_rpc_status status);

void forensics_view_render(
    struct nk_context *ctx,
    frontend_forensics_state *state,
    int width,
    int height,
    const frontend_forensics_land_context *land);

/* After machine_state: explain clamp/quantize using post-land focus. */
void forensics_view_apply_land_focus(
    frontend_forensics_state *state,
    const frontend_forensics_land_context *land);

#ifdef __cplusplus
}
#endif

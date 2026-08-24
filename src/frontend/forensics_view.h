#pragma once

#include "nuklear_config.h"

#include <stdbool.h>
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
    FRONTEND_FR_STATUS_MAX = 192
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
    char status[FRONTEND_FR_STATUS_MAX];
    bool request_close; /* Close button → leave to entry surface */
    bool request_host_pause; /* set on open; main pauses if still running */
} frontend_forensics_state;

/* Result of leaving Forensics (main applies ui_visible + run/pause). */
typedef struct frontend_forensics_leave_result {
    bool show_debugger; /* true → debugger; false → full-screen CRT */
    bool resume_machine; /* true only when returning to CRT that was running */
} frontend_forensics_leave_result;

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

void forensics_view_render(
    struct nk_context *ctx,
    frontend_forensics_state *state,
    int width,
    int height);

#ifdef __cplusplus
}
#endif

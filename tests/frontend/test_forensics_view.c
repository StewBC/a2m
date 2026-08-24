/* Forensics shell state: entry surface, CRT resume latch, query history. */
#include "forensics_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

int main(void)
{
    frontend_forensics_state state;
    frontend_forensics_leave_result leave;

    forensics_view_init(&state);
    expect_true("init closed", !forensics_view_is_open(&state));

    /* CRT entry while running → Opt+R restores run. */
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_CRT, true);
    expect_true("open", forensics_view_is_open(&state));
    expect_true("entry crt", forensics_view_entry(&state) == FRONTEND_FORENSICS_ENTRY_CRT);
    expect_true("crt was running", forensics_view_crt_was_running(&state));
    expect_true("pause requested", state.request_host_pause);
    leave = forensics_view_leave_to_entry(&state);
    expect_true("leave entry closed", !forensics_view_is_open(&state));
    expect_true("leave entry crt", !leave.show_debugger);
    expect_true("leave entry resume", leave.resume_machine);

    /* CRT entry while paused → Opt+R stays paused. */
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_CRT, false);
    leave = forensics_view_leave_to_entry(&state);
    expect_true("crt paused no resume", !leave.show_debugger && !leave.resume_machine);

    /* Debugger entry → Opt+R returns debugger, no resume. */
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, true);
    expect_true("entry dbg", forensics_view_entry(&state) == FRONTEND_FORENSICS_ENTRY_DEBUGGER);
    expect_true("dbg ignores crt flag", !forensics_view_crt_was_running(&state));
    leave = forensics_view_leave_to_entry(&state);
    expect_true("leave dbg", leave.show_debugger && !leave.resume_machine);

    /* F9 from CRT entry → debugger, no resume (abandons CRT latch). */
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_CRT, true);
    leave = forensics_view_leave_to_debugger(&state);
    expect_true("f9 debugger", leave.show_debugger && !leave.resume_machine);

    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    forensics_view_query_history_push(&state, "address=$2011 access=data-write");
    forensics_view_query_history_push(&state, "pc=$FC00");
    expect_true("hist count", state.query_history_count == 2u);
    expect_true(
        "hist newest",
        strcmp(state.query_history[0], "pc=$FC00") == 0);
    expect_true(
        "hist older",
        strcmp(state.query_history[1], "address=$2011 access=data-write") == 0);

    expect_true("prev", forensics_view_query_history_prev(&state));
    expect_true("prev query", strcmp(state.query, "pc=$FC00") == 0);
    expect_true("prev2", forensics_view_query_history_prev(&state));
    expect_true(
        "prev2 query",
        strcmp(state.query, "address=$2011 access=data-write") == 0);
    expect_true("next", forensics_view_query_history_next(&state));
    expect_true("next query", strcmp(state.query, "pc=$FC00") == 0);
    expect_true("next live", forensics_view_query_history_next(&state));
    expect_true("live empty", state.query[0] == '\0');

    forensics_view_clear_transcript(&state);
    expect_true("clear logical", state.logical_count == 0u);
    expect_true("clear sel", !state.has_land_selection);
    forensics_view_close(&state);
    expect_true("closed", !forensics_view_is_open(&state));

    printf("ok\n");
    return 0;
}

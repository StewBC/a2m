/* Forensics shell state: open/close latch, clear, query history. */
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

    forensics_view_init(&state);
    expect_true("init closed", !forensics_view_is_open(&state));
    expect_true("init no latch", !forensics_view_resume_on_exit(&state));

    forensics_view_open(&state, true);
    expect_true("open", forensics_view_is_open(&state));
    expect_true("latch", forensics_view_resume_on_exit(&state));
    expect_true("focus pending", state.query_focus_pending);

    expect_true("close returns latch", forensics_view_close(&state));
    expect_true("closed", !forensics_view_is_open(&state));
    expect_true("latch cleared", !forensics_view_resume_on_exit(&state));

    forensics_view_open(&state, false);
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
    expect_true("close no latch", !forensics_view_close(&state));

    printf("ok\n");
    return 0;
}

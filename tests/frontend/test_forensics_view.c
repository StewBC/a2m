/* Forensics: shell, query parse, HST1 format golden, transcript apply. */
#include "forensics_view.h"

#include "runtime_history.h"

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

static void expect_streq(const char *name, const char *got, const char *want)
{
    if (got == NULL || want == NULL || strcmp(got, want) != 0) {
        fprintf(
            stderr,
            "FAIL: %s\n  got:  [%s]\n  want: [%s]\n",
            name,
            got != NULL ? got : "(null)",
            want != NULL ? want : "(null)");
        exit(1);
    }
}

static void fill_instruction(runtime_history_record *r, uint64_t epoch)
{
    memset(r, 0, sizeof(*r));
    r->epoch = epoch;
    r->id = 13523u;
    r->timeline = 1u;
    r->machine_cycle = 1234u;
    r->kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    r->pc = 0xfcacu;
    r->a = 0x00u;
    r->x = 0x00u;
    r->y = 0x00u;
    r->sp = 0xf2u;
    r->p = 0x24u;
    r->opcode = 0xd0u;
    r->operand1 = 0x05u;
    r->operand2 = 0x00u;
    r->instruction_length = 2u;
    r->access_count = 2u;
    r->accesses[0].address = 0xc000u;
    r->accesses[0].cycle_offset = 1u;
    r->accesses[0].value = 0x22u;
    r->accesses[0].kind = C6510_BUS_ACCESS_DATA_WRITE;
    r->accesses[1].address = 0xfcadu;
    r->accesses[1].cycle_offset = 0u;
    r->accesses[1].value = 0x05u;
    r->accesses[1].kind = C6510_BUS_ACCESS_OPERAND_READ;
    r->partial = false;
    r->access_truncated = true;
    r->timing_truncated = false;
}

static void fill_marker(runtime_history_record *r, uint64_t epoch)
{
    memset(r, 0, sizeof(*r));
    r->epoch = epoch;
    r->id = 13524u;
    r->timeline = 1u;
    r->machine_cycle = 1200u;
    r->kind = RUNTIME_HISTORY_RECORD_MARKER;
    r->marker_kind = RUNTIME_HISTORY_MARKER_MEDIA_CHANGED;
    r->marker_arg0 = 1u;
    r->marker_arg1 = (6u << 8) | 0u;
    r->access_count = 0u;
}

static void test_shell(void)
{
    frontend_forensics_state state;
    frontend_forensics_leave_result leave;

    forensics_view_init(&state);
    expect_true("init closed", !forensics_view_is_open(&state));

    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_CRT, true);
    expect_true("open", forensics_view_is_open(&state));
    expect_true("entry crt", forensics_view_entry(&state) == FRONTEND_FORENSICS_ENTRY_CRT);
    expect_true("crt was running", forensics_view_crt_was_running(&state));
    expect_true("pause requested", state.request_host_pause);
    leave = forensics_view_leave_to_entry(&state);
    expect_true("leave entry closed", !forensics_view_is_open(&state));
    expect_true("leave entry crt", !leave.show_debugger);
    expect_true("leave entry resume", leave.resume_machine);

    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_CRT, false);
    leave = forensics_view_leave_to_entry(&state);
    expect_true("crt paused no resume", !leave.show_debugger && !leave.resume_machine);

    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, true);
    expect_true("entry dbg", forensics_view_entry(&state) == FRONTEND_FORENSICS_ENTRY_DEBUGGER);
    expect_true("dbg ignores crt flag", !forensics_view_crt_was_running(&state));
    leave = forensics_view_leave_to_entry(&state);
    expect_true("leave dbg", leave.show_debugger && !leave.resume_machine);

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
}

static void test_parse(void)
{
    frontend_forensics_query_parsed p;

    expect_true("empty", forensics_view_parse_query("", 0u, &p) && p.empty);

    expect_true("info", forensics_view_parse_query("info", 0u, &p));
    expect_true("info verb", p.verb_code == 4);

    expect_true(
        "find bare",
        forensics_view_parse_query("address=$2011 access=data-write", 0u, &p));
    expect_true("find verb", p.verb_code == 1);
    expect_true("find has addr", p.query.has_address);
    expect_true("find addr", p.query.address_first == 0x2011u);

    expect_true(
        "find verb",
        forensics_view_parse_query("find pc=$FC00 limit=32", 0u, &p));
    expect_true("find limit", p.limit == 32u);
    expect_true("find has pc", p.query.has_pc);

    expect_true("next needs cursor", !forensics_view_parse_query("next", 0u, &p));
    expect_true("next ok", forensics_view_parse_query("next limit=16", 99u, &p));
    expect_true("next verb", p.verb_code == 2);
    expect_true("next limit", p.limit == 16u);

    expect_true("read", forensics_view_parse_query("read 42 before=2 after=3", 0u, &p));
    expect_true("read verb", p.verb_code == 3);
    expect_true("read id", p.read_id == 42u);
    expect_true("read before", p.before == 2u);
    expect_true("read after", p.after == 3u);

    expect_true("bad key", !forensics_view_parse_query("nope=1", 0u, &p));
}

static void test_format_golden(void)
{
    runtime_history_record instr;
    runtime_history_record marker;
    char line[FRONTEND_FR_FORMAT_CAP];
    /* Matches Ctl.format_hst1_record(..., compact=True). */
    static const char *want_instr =
        "id=13523 pc=$FCAC a=00 x=00 y=00 sp=F2 p=24 opcode=$D0 cyc=1234 "
        "[access_truncated] accesses: write $C000=22 @+1";
    static const char *want_marker =
        "id=13524 kind=reserved3 cyc=1200 marker=13 arg0=1 arg1=1536";

    fill_instruction(&instr, 7u);
    fill_marker(&marker, 7u);

    (void)forensics_format_hst1_record(line, sizeof(line), &instr, false, true);
    expect_streq("instr compact", line, want_instr);

    (void)forensics_format_hst1_record(line, sizeof(line), &marker, false, true);
    expect_streq("marker", line, want_marker);
}

static void test_apply_and_select(void)
{
    frontend_forensics_state state;
    runtime_history_record records[2];
    runtime_history_rpc_meta meta;
    bool anchors[2] = {true, false};
    unsigned i;

    forensics_view_init(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    fill_instruction(&records[0], 7u);
    fill_marker(&records[1], 7u);
    memset(&meta, 0, sizeof(meta));
    meta.status = RUNTIME_HISTORY_RPC_OK;
    meta.epoch = 7u;
    meta.count = 2u;
    meta.cursor = 13524u;
    meta.more = 1u;
    meta.oldest = 1u;
    meta.newest = 13524u;

    forensics_view_apply_result(
        &state,
        1,
        "find address=$C000 access=write",
        &meta,
        records,
        2u,
        anchors);
    expect_true("cursor", state.last_cursor == 13524u);
    expect_true("more", state.last_more);
    expect_true("logical>0", state.logical_count >= 4u);
    expect_true("display>0", state.display_count > 0u);

    /* Click first record entry (skip blank + header + meta). */
    for (i = 0u; i < state.logical_count; ++i) {
        if (state.logical[i].is_record) {
            state.sel_logical_first = i;
            state.sel_logical_last = i;
            state.has_land_selection = state.logical[i].has_cycle;
            state.selected_cycle = state.logical[i].cycle;
            state.selected_id = state.logical[i].id;
            break;
        }
    }
    expect_true("land sel", state.has_land_selection);
    expect_true("land cyc", state.selected_cycle == 1234u);

    /* Header selects whole block. */
    for (i = 0u; i < state.logical_count; ++i) {
        if (state.logical[i].is_header) {
            unsigned last = i;
            while (last + 1u < state.logical_count &&
                   !state.logical[last + 1u].is_header) {
                last++;
            }
            state.sel_logical_first = i;
            state.sel_logical_last = last;
            expect_true("block span", last > i);
            break;
        }
    }

    forensics_view_apply_rpc_error(&state, RUNTIME_HISTORY_RPC_MACHINE_RUNNING);
    expect_streq("err running", state.status, "machine-running");

    {
        snprintf(state.query, sizeof(state.query), "acc");
        expect_true("tab key", forensics_view_autocomplete(&state));
        expect_streq("tab access", state.query, "access=");
        expect_true("tab rewrite", state.query_rewrite_pending);
        snprintf(state.query, sizeof(state.query), "access=data-w");
        expect_true("tab access val", forensics_view_autocomplete(&state));
        expect_streq("tab data-write", state.query, "access=data-write");
        /* LCP across address+access: "a" alone does not grow. */
        snprintf(state.query, sizeof(state.query), "a");
        expect_true("tab ambiguous", !forensics_view_autocomplete(&state));
        /* "ad" uniquely grows toward address. */
        snprintf(state.query, sizeof(state.query), "ad");
        expect_true("tab addr lcp", forensics_view_autocomplete(&state));
        expect_streq("tab address", state.query, "address=");
    }

    forensics_view_clear_transcript(&state);
    forensics_view_close(&state);
}

static void test_land_focus_status(void)
{
    frontend_forensics_state state;
    frontend_forensics_land_context land;

    forensics_view_init(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    state.land_awaiting_focus = true;
    state.land_requested_cycle = 1234u;
    memset(&land, 0, sizeof(land));
    land.inspecting = true;
    land.window_valid = true;
    land.focus_cycle = 1200u;
    land.oldest_cycle = 100u;
    land.newest_cycle = 9999u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("await cleared", !state.land_awaiting_focus);
    expect_true(
        "quantized status",
        strstr(state.status, "focus_cycle=1200") != NULL &&
            strstr(state.status, "before/quantized") != NULL);

    state.land_awaiting_focus = true;
    state.land_requested_cycle = 50u;
    land.focus_cycle = 100u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("clamp status", strstr(state.status, "clamped oldest") != NULL);

    state.land_awaiting_focus = true;
    state.land_requested_cycle = 20000u;
    land.focus_cycle = 9999u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("live status", strstr(state.status, "live") != NULL);

    state.land_awaiting_focus = true;
    state.land_awaiting_exact = false;
    state.land_requested_cycle = 500u;
    land.focus_cycle = 500u;
    forensics_view_apply_land_focus(&state, &land);
    expect_streq("land match", state.status, "landed focus_cycle=500");

    state.land_awaiting_focus = true;
    state.land_awaiting_exact = true;
    state.land_requested_cycle = 500u;
    land.focus_cycle = 500u;
    forensics_view_apply_land_focus(&state, &land);
    expect_streq("exact match", state.status, "landed exact focus_cycle=500");

    state.land_awaiting_focus = true;
    state.land_awaiting_exact = true;
    state.land_requested_cycle = 1500u;
    land.focus_cycle = 1200u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("partial exact", strstr(state.status, "partial exact") != NULL);

    forensics_view_close(&state);
}

int main(void)
{
    test_shell();
    test_parse();
    test_format_golden();
    test_apply_and_select();
    test_land_focus_status();
    printf("ok\n");
    return 0;
}

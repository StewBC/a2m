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

static void expect_contains(const char *name, const char *got, const char *needle)
{
    if (got == NULL || needle == NULL || strstr(got, needle) == NULL) {
        fprintf(
            stderr,
            "FAIL: %s\n  got:  [%s]\n  need: [%s]\n",
            name,
            got != NULL ? got : "(null)",
            needle != NULL ? needle : "(null)");
        exit(1);
    }
}

static const char k_help_verbs[] = "verbs: find | next | read | info";
static const char k_help_pc_addr[] = "pc/address: u16 or lo-hi ($hex ok)";
static const char k_help_read[] =
    "read <id> [before=N] [after=N] [epoch=N]";
static const char k_help_info[] = "info takes no args";
static const char k_help_next[] = "next [limit=1..256]";
static const char k_help_limit[] = "limit: 1..256";
static const char k_help_before_after[] = "before/after: 0..256";
static const char k_help_enter[] = "Enter to run";
static const char k_help_access[] = "access:";

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
    expect_true("info extra", !forensics_view_parse_query("info foo", 0u, &p));
    expect_streq("info extra err", p.error, "bad-args");

    expect_true(
        "implicit find rejected",
        !forensics_view_parse_query("address=$2011 access=data-write", 0u, &p));
    expect_streq("implicit find err", p.error, k_help_verbs);
    expect_true("prefix f not find", !forensics_view_parse_query("f", 0u, &p));
    expect_streq("prefix f err", p.error, k_help_verbs);

    expect_true(
        "find verb",
        forensics_view_parse_query("find pc=$FC00 limit=32", 0u, &p));
    expect_true("find verb code", p.verb_code == 1);
    expect_true("find limit", p.limit == 32u);
    expect_true("find has pc", p.query.has_pc);
    expect_true(
        "find case",
        forensics_view_parse_query("Find address=$2011", 0u, &p));
    expect_true("find case addr", p.query.has_address && p.query.address_first == 0x2011u);
    expect_true("find defaults", forensics_view_parse_query("find", 0u, &p));
    expect_true("find defaults verb", p.verb_code == 1);

    expect_true("next needs cursor", !forensics_view_parse_query("next", 0u, &p));
    expect_true("next ok", forensics_view_parse_query("next limit=16", 99u, &p));
    expect_true("next verb", p.verb_code == 2);
    expect_true("next limit", p.limit == 16u);

    expect_true("read", forensics_view_parse_query("read 42 before=2 after=3", 0u, &p));
    expect_true("read verb", p.verb_code == 3);
    expect_true("read id", p.read_id == 42u);
    expect_true("read before", p.before == 2u);
    expect_true("read after", p.after == 3u);
    expect_true(
        "read keys first",
        forensics_view_parse_query("read before=2 42", 0u, &p));
    expect_true("read keys first id", p.read_id == 42u && p.before == 2u);
    expect_true(
        "read needs id",
        !forensics_view_parse_query("read before=", 0u, &p));
    expect_streq("read needs id err", p.error, "bad-args (read needs id)");
    expect_true(
        "read empty value",
        !forensics_view_parse_query("read 42 before=", 0u, &p));
    expect_streq("read empty value err", p.error, "bad-args");

    expect_true("bad key", !forensics_view_parse_query("nope=1", 0u, &p));
    expect_streq("bad key err", p.error, k_help_verbs);
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
        snprintf(state.query, sizeof(state.query), "find acc");
        expect_true("tab key", forensics_view_autocomplete(&state));
        expect_streq("tab access", state.query, "find access=");
        expect_true("tab rewrite", state.query_rewrite_pending);
        expect_contains("tab access help", state.status, k_help_access);
        snprintf(state.query, sizeof(state.query), "find access=data-w");
        expect_true("tab access val", forensics_view_autocomplete(&state));
        expect_streq("tab data-write", state.query, "find access=data-write");
        snprintf(state.query, sizeof(state.query), "find a");
        expect_true("tab ambiguous", !forensics_view_autocomplete(&state));
        expect_contains("tab ambiguous help", state.status, "find keys:");
        snprintf(state.query, sizeof(state.query), "find ad");
        expect_true("tab addr unique", forensics_view_autocomplete(&state));
        expect_streq("tab address", state.query, "find address=");
        expect_streq("tab address help", state.status, k_help_pc_addr);
    }

    forensics_view_clear_transcript(&state);
    forensics_view_close(&state);
}

static void test_token_at_offset(void)
{
    char token[64];
    static const char *line =
        "id=13523 pc=$FCAC a=00 x=00 y=00 sp=F2 p=24 opcode=$D0 cyc=1234 "
        "[access_truncated] accesses: write $C000=22 @+1";
    const char *id = strstr(line, "id=");
    const char *pc = strstr(line, "pc=$");
    const char *cyc = strstr(line, "cyc=");
    const char *acc = strstr(line, "accesses:");

    expect_true("anchors", id != NULL && pc != NULL && cyc != NULL && acc != NULL);
    expect_true(
        "id token",
        forensics_token_at_offset(
            line, (size_t)(id - line) + 3u, token, sizeof(token)));
    expect_streq("id value", token, "id=13523");
    expect_true(
        "pc token",
        forensics_token_at_offset(
            line, (size_t)(pc - line) + 4u, token, sizeof(token)));
    expect_streq("pc value", token, "pc=$FCAC");
    expect_true(
        "cyc token",
        forensics_token_at_offset(
            line, (size_t)(cyc - line) + 4u, token, sizeof(token)));
    expect_streq("cyc value", token, "cyc=1234");
    expect_true(
        "no token on accesses",
        !forensics_token_at_offset(
            line, (size_t)(acc - line) + 2u, token, sizeof(token)));
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
    expect_true("leave dbg after land", state.request_leave_debugger);

    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    state.land_requested_cycle = 50u;
    land.focus_cycle = 100u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("clamp status", strstr(state.status, "clamped oldest") != NULL);
    expect_true("leave dbg after clamp", state.request_leave_debugger);

    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    state.land_requested_cycle = 20000u;
    land.focus_cycle = 9999u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("live status", strstr(state.status, "live") != NULL);
    expect_true("leave dbg after live", state.request_leave_debugger);

    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    state.land_awaiting_exact = false;
    state.land_requested_cycle = 500u;
    land.focus_cycle = 500u;
    forensics_view_apply_land_focus(&state, &land);
    expect_streq("land match", state.status, "landed focus_cycle=500");
    expect_true("leave dbg after match", state.request_leave_debugger);

    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    state.land_awaiting_exact = true;
    state.land_requested_cycle = 500u;
    land.focus_cycle = 500u;
    forensics_view_apply_land_focus(&state, &land);
    expect_streq("exact match", state.status, "landed exact focus_cycle=500");
    expect_true("leave dbg after exact", state.request_leave_debugger);

    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    state.land_awaiting_exact = true;
    state.land_requested_cycle = 1500u;
    land.focus_cycle = 1200u;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("partial exact", strstr(state.status, "partial exact") != NULL);
    expect_true("leave dbg after partial", state.request_leave_debugger);

    /* Not inspecting yet: keep awaiting; do not request Debug leave. */
    state.request_leave_debugger = false;
    state.land_awaiting_focus = true;
    land.inspecting = false;
    forensics_view_apply_land_focus(&state, &land);
    expect_true("still awaiting", state.land_awaiting_focus);
    expect_true("no leave while not inspecting", !state.request_leave_debugger);

    forensics_view_close(&state);
    expect_true("close clears leave", !state.request_leave_debugger);
}

static float test_font_width(
    nk_handle handle,
    float height,
    const char *text,
    int len)
{
    (void)handle;
    (void)height;
    (void)text;
    return (float)len * 7.0f;
}

static void tab_case(
    const char *name,
    const char *input,
    const char *want_line,
    const char *status_sub,
    int want_rewrite)
{
    frontend_forensics_state state;
    int rewrote;

    forensics_view_init(&state);
    snprintf(state.query, sizeof(state.query), "%s", input);
    rewrote = forensics_view_autocomplete(&state) ? 1 : 0;
    expect_streq(name, state.query, want_line);
    if (want_rewrite) {
        expect_true(name, rewrote == 1 && state.query_rewrite_pending);
    } else {
        expect_true(name, rewrote == 0 && !state.query_rewrite_pending);
    }
    expect_contains(name, state.status, status_sub);
}

static void test_query_guide(void)
{
    frontend_forensics_query_parsed p;

    tab_case("empty", "", "", k_help_verbs, 0);
    tab_case("f", "f", "find ", "find keys:", 1);
    tab_case("n", "n", "next ", k_help_next, 1);
    tab_case("r", "r", "read ", k_help_read, 1);
    tab_case("i", "i", "info ", k_help_info, 1);
    tab_case("re", "re", "read ", k_help_read, 1);
    tab_case("xyz", "xyz", "xyz", k_help_verbs, 0);
    tab_case(
        "bare kv",
        "address=$4000",
        "address=$4000",
        k_help_verbs,
        0);
    tab_case(
        "f add=$4000",
        "f add=$4000",
        "find address=$4000",
        "find keys:",
        1);
    tab_case("find add", "find add", "find address=", k_help_pc_addr, 1);
    tab_case("find a", "find a", "find a", "find keys:", 0);
    tab_case(
        "whole line",
        "find add=$4000 acc=re",
        "find address=$4000 access=read",
        "find keys:",
        1);
    tab_case(
        "a not unique",
        "find a=$4000 acc=re",
        "find a=$4000 access=read",
        "find keys:",
        1);
    tab_case(
        "access r",
        "find add=$4000 acc=r",
        "find address=$4000 access=r",
        k_help_access,
        1);
    tab_case("read bef", "read bef", "read before=", k_help_before_after, 1);
    tab_case(
        "read id space",
        "read 12345 ",
        "read 12345 ",
        "read keys:",
        0);
    tab_case("info space", "info ", "info ", k_help_info, 0);
    tab_case("info exact", "info", "info", k_help_enter, 0);
    tab_case("next lim", "next lim", "next limit=", k_help_limit, 1);
    tab_case(
        "no lcp data",
        "find access=da",
        "find access=da",
        k_help_access,
        0);
    tab_case(
        "no lcp data exact",
        "find access=data",
        "find access=data",
        k_help_access,
        0);
    tab_case("find add space", "find add ", "find address=", k_help_pc_addr, 1);

    expect_true(
        "enter implicit",
        !forensics_view_parse_query("address=$4000", 0u, &p));
    expect_streq("enter implicit err", p.error, k_help_verbs);
    expect_true(
        "enter read before=",
        !forensics_view_parse_query("read before=", 0u, &p));
    expect_streq("enter read before= err", p.error, "bad-args (read needs id)");
    expect_true(
        "enter read keys first",
        forensics_view_parse_query("read before=2 42", 0u, &p));
    expect_true("enter read keys first id", p.read_id == 42u && p.before == 2u);

    {
        frontend_forensics_state state;
        forensics_view_init(&state);
        snprintf(
            state.query,
            sizeof(state.query),
            "find add=$4000 acc=re");
        expect_true("omit used tab", forensics_view_autocomplete(&state));
        expect_true(
            "omit address=",
            strstr(state.status, "address=") == NULL);
        expect_true(
            "omit access=",
            strstr(state.status, "access=") == NULL);
        expect_contains("keep pc=", state.status, "pc=");
    }
}

/* Headless: Tab autocomplete must move the query caret to the new end. */
static void test_tab_autocomplete_cursor(void)
{
    struct nk_context ctx;
    struct nk_user_font font;
    frontend_forensics_state state;
    frontend_forensics_land_context land;
    struct nk_window *win;

    memset(&font, 0, sizeof(font));
    font.userdata = nk_handle_ptr(NULL);
    font.height = 12.0f;
    font.width = test_font_width;
    expect_true("nk init", nk_init_default(&ctx, &font) != 0);

    forensics_view_init(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    memset(&land, 0, sizeof(land));

    nk_input_begin(&ctx);
    nk_input_end(&ctx);
    forensics_view_render(&ctx, &state, 900, 600, &land);
    win = nk_window_find(&ctx, "Forensics");
    expect_true("forensics win", win != NULL);
    /* Simulate caret after typing a verb-first key prefix. */
    snprintf(state.query, sizeof(state.query), "find ad");
    win->edit.cursor = (int)strlen(state.query);
    win->edit.sel_start = win->edit.cursor;
    win->edit.sel_end = win->edit.cursor;
    nk_clear(&ctx);

    expect_true("tab complete", forensics_view_autocomplete(&state));
    expect_streq("tab address", state.query, "find address=");
    expect_true("tab rewrite pending", state.query_rewrite_pending);

    nk_input_begin(&ctx);
    nk_input_end(&ctx);
    forensics_view_render(&ctx, &state, 900, 600, &land);
    win = nk_window_find(&ctx, "Forensics");
    expect_true("forensics win after tab", win != NULL);
    expect_true(
        "caret after address=",
        win->edit.cursor == (int)strlen("find address="));
    expect_true("rewrite consumed", !state.query_rewrite_pending);
    nk_clear(&ctx);

    forensics_view_close(&state);
    nk_free(&ctx);
}

/* Leave/return must restore the transcript group scrollbar (Help pattern).
 * nk_group_set/get_scroll need ctx->current, so assert via state.transcript_scroll_y
 * which render updates from the live group each frame. */
static void test_transcript_scroll_restore(void)
{
    struct nk_context ctx;
    struct nk_user_font font;
    frontend_forensics_state state;
    frontend_forensics_land_context land;
    runtime_history_record records[1];
    runtime_history_rpc_meta meta;
    bool anchors[1] = {true};
    const nk_uint want_y = 240u;
    unsigned i;

    memset(&font, 0, sizeof(font));
    font.userdata = nk_handle_ptr(NULL);
    font.height = 12.0f;
    font.width = test_font_width;
    expect_true("nk init scroll", nk_init_default(&ctx, &font) != 0);

    forensics_view_init(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    memset(&land, 0, sizeof(land));
    expect_true("open pending scroll", state.pending_scroll_restore);

    for (i = 0u; i < 40u; ++i) {
        fill_instruction(&records[0], 7u);
        records[0].id = 1000u + i;
        records[0].machine_cycle = 2000u + i;
        memset(&meta, 0, sizeof(meta));
        meta.status = RUNTIME_HISTORY_RPC_OK;
        meta.epoch = 7u;
        meta.count = 1u;
        meta.cursor = records[0].id;
        meta.more = 0u;
        meta.oldest = 1u;
        meta.newest = records[0].id;
        forensics_view_apply_result(
            &state,
            1,
            "find address=$C000",
            &meta,
            records,
            1u,
            anchors);
    }
    expect_true("tall transcript", state.display_count > 60u);

    nk_input_begin(&ctx);
    nk_input_end(&ctx);
    forensics_view_render(&ctx, &state, 900, 600, &land);
    expect_true("scroll restore consumed", !state.pending_scroll_restore);
    expect_true("starts at top", state.transcript_scroll_y == 0u);
    nk_clear(&ctx);

    /* Simulate a user scroll that was stored before leave. */
    state.transcript_scroll_y = want_y;
    forensics_view_close(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    expect_true("reopen pending", state.pending_scroll_restore);
    expect_true("scroll kept across close", state.transcript_scroll_y == want_y);

    nk_input_begin(&ctx);
    nk_input_end(&ctx);
    forensics_view_render(&ctx, &state, 900, 600, &land);
    expect_true("scroll restored", state.transcript_scroll_y == want_y);
    expect_true("pending cleared", !state.pending_scroll_restore);
    nk_clear(&ctx);

    /* Second open without changing scroll must keep the same offset. */
    forensics_view_close(&state);
    forensics_view_open(&state, FRONTEND_FORENSICS_ENTRY_DEBUGGER, false);
    nk_input_begin(&ctx);
    nk_input_end(&ctx);
    forensics_view_render(&ctx, &state, 900, 600, &land);
    expect_true("scroll sticky", state.transcript_scroll_y == want_y);
    nk_clear(&ctx);

    forensics_view_clear_transcript(&state);
    expect_true("clear resets scroll", state.transcript_scroll_y == 0u);
    expect_true("clear pending restore", state.pending_scroll_restore);

    forensics_view_close(&state);
    nk_free(&ctx);
}

int main(void)
{
    test_shell();
    test_parse();
    test_format_golden();
    test_apply_and_select();
    test_token_at_offset();
    test_land_focus_status();
    test_query_guide();
    test_tab_autocomplete_cursor();
    test_transcript_scroll_restore();
    printf("ok\n");
    return 0;
}

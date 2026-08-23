/* TM1: HST1 tape queries. Fill history programmatically; do not mutate apple2. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "runtime_history.h"
#include "runtime_internal.h"
#include "runtime_timemachine.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static runtime *make_runtime(void)
{
    runtime_config config;
    runtime *rt;

    runtime_config_init(&config);
    config.start_running = false;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 0;
    config.frame_ring_memory_mb_configured = true;
    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("history", rt->history != NULL);
    return rt;
}

static uint64_t emit_insn(
    runtime_history *history,
    uint64_t cycle,
    uint16_t pc,
    uint8_t sp,
    uint8_t opcode)
{
    runtime_history_begin begin;
    runtime_history_record rec;

    memset(&begin, 0, sizeof(begin));
    begin.kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    begin.machine_cycle = cycle;
    begin.pc = pc;
    begin.sp = sp;
    expect_true("begin", runtime_history_begin_record(history, &begin));
    expect_true(
        "opcode fetch",
        runtime_history_append_access(
            history, C6510_BUS_ACCESS_OPCODE_FETCH, pc, opcode, cycle));
    expect_true("complete", runtime_history_complete_record(history));
    expect_true("last", runtime_history_last(history, &rec));
    return rec.id;
}

static uint64_t emit_irq(runtime_history *history, uint64_t cycle, uint8_t sp)
{
    runtime_history_begin begin;
    runtime_history_record rec;

    memset(&begin, 0, sizeof(begin));
    begin.kind = RUNTIME_HISTORY_RECORD_IRQ;
    begin.machine_cycle = cycle;
    begin.sp = sp;
    expect_true("irq begin", runtime_history_begin_record(history, &begin));
    expect_true("irq complete", runtime_history_complete_record(history));
    expect_true("irq last", runtime_history_last(history, &rec));
    return rec.id;
}

static void query_ok(
    runtime *rt,
    runtime_tm_query_op op,
    const runtime_tm_query_args *args,
    runtime_tm_query_result *out)
{
    runtime_tm_query_status st = runtime_tm_query(rt, op, args, out);
    expect_true("query ok", st == RUNTIME_TM_QUERY_OK);
    expect_true("focus valid", out->focus.valid);
}

static int wait_tm_focus(
    runtime_client *client,
    uint64_t token,
    runtime_event *out,
    double timeout_s)
{
    clock_t start = clock();
    runtime_event event;

    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_TM_FOCUS &&
                event.request_token == token) {
                if (out != NULL) {
                    *out = event;
                }
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

int main(void)
{
    runtime *rt;
    runtime_tm_query_args args;
    runtime_tm_query_result result;
    runtime_tm_window window;
    uint64_t id_jsr;
    uint64_t id_callee;
    uint64_t id_rts;
    uint64_t id_after;
    uint64_t id_other;

    rt = make_runtime();

    /* Empty tape. */
    expect_true(
        "empty",
        runtime_tm_query(rt, RUNTIME_TM_QUERY_STEP, NULL, &result) ==
            RUNTIME_TM_QUERY_EMPTY);
    runtime_tm_window_info(rt, &window);
    expect_true("empty window", !window.valid);

    /*
     * Nested call, SP-depth:
     *   JSR  pc=0x0300 sp=0xF0
     *   IRQ  (skipped for depth)
     *   LDA  pc=0x0306 sp=0xEE   callee
     *   RTS  pc=0x0308 sp=0xEE
     *   LDA  pc=0x0303 sp=0xF0   caller after return
     *   LDA  pc=0x0400 sp=0xF0   run-to target later
     */
    id_jsr = emit_insn(rt->history, 1000u, 0x0300u, 0xF0u, 0x20u);
    (void)emit_irq(rt->history, 1004u, 0xEBu);
    id_callee = emit_insn(rt->history, 1010u, 0x0306u, 0xEEu, 0xA9u);
    id_rts = emit_insn(rt->history, 1020u, 0x0308u, 0xEEu, 0x60u);
    id_after = emit_insn(rt->history, 1030u, 0x0303u, 0xF0u, 0xA9u);
    id_other = emit_insn(rt->history, 1040u, 0x0400u, 0xF0u, 0xA9u);
    (void)id_rts;

    runtime_tm_window_info(rt, &window);
    expect_true("window valid", window.valid);
    expect_true("window oldest id 1", window.oldest_id >= 1u);
    expect_true("window newest", window.newest_id == id_other);
    expect_true("window cycles", window.oldest_cycle == 1000u &&
        window.newest_cycle == 1040u);

    /* Seek to JSR. */
    memset(&args, 0, sizeof(args));
    args.history_id = id_jsr;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    expect_true("seek jsr id", result.focus.history_id == id_jsr);
    expect_true("seek jsr pc", result.focus.pc == 0x0300u);
    expect_true("seek jsr opcode", result.focus.opcode == 0x20u);

    /* Step skips IRQ and lands on callee. */
    memset(&args, 0, sizeof(args));
    args.direction = 1;
    query_ok(rt, RUNTIME_TM_QUERY_STEP, &args, &result);
    expect_true("step to callee", result.focus.history_id == id_callee);
    expect_true("step pc", result.focus.pc == 0x0306u);

    /* Step back to JSR (skip IRQ). */
    args.direction = -1;
    query_ok(rt, RUNTIME_TM_QUERY_STEP, &args, &result);
    expect_true("step back jsr", result.focus.history_id == id_jsr);

    /* Step-over from JSR lands on caller after return (sp returned). */
    query_ok(rt, RUNTIME_TM_QUERY_STEP_OVER, NULL, &result);
    expect_true("over id", result.focus.history_id == id_after);
    expect_true("over pc", result.focus.pc == 0x0303u);
    expect_true("over sp", result.focus.sp == 0xF0u);

    /* Step-over when not on JSR ≡ step. */
    query_ok(rt, RUNTIME_TM_QUERY_STEP_OVER, NULL, &result);
    expect_true("over-as-step", result.focus.history_id == id_other);

    /* Seek callee, step-out to caller (sp > entry). */
    memset(&args, 0, sizeof(args));
    args.history_id = id_callee;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    query_ok(rt, RUNTIME_TM_QUERY_STEP_OUT, NULL, &result);
    expect_true("out id", result.focus.history_id == id_after);
    expect_true("out pc", result.focus.pc == 0x0303u);

    /* Run-to PC 0x0400 from JSR. */
    memset(&args, 0, sizeof(args));
    args.history_id = id_jsr;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    memset(&args, 0, sizeof(args));
    args.target_pc = 0x0400u;
    query_ok(rt, RUNTIME_TM_QUERY_RUN_TO_PC, &args, &result);
    expect_true("run-to pc", result.focus.pc == 0x0400u);
    expect_true("run-to id", result.focus.history_id == id_other);

    /* Missing run-to target: end-of-tape, focus stays at last insn. */
    memset(&args, 0, sizeof(args));
    args.history_id = id_jsr;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    memset(&args, 0, sizeof(args));
    args.target_pc = 0xFFFFu;
    expect_true(
        "run-to miss",
        runtime_tm_query(rt, RUNTIME_TM_QUERY_RUN_TO_PC, &args, &result) ==
            RUNTIME_TM_QUERY_END_OF_TAPE);
    expect_true("miss focus last", result.focus.history_id == id_other);

    /* Cycle ceiling miss. */
    memset(&args, 0, sizeof(args));
    args.history_id = id_jsr;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    memset(&args, 0, sizeof(args));
    args.target_pc = 0x0400u;
    args.cycle_ceiling = 1015u;
    expect_true(
        "run-to ceiling",
        runtime_tm_query(rt, RUNTIME_TM_QUERY_RUN_TO_PC, &args, &result) ==
            RUNTIME_TM_QUERY_END_OF_TAPE);

    /* Seek cycle: nearest insn <= target. */
    memset(&args, 0, sizeof(args));
    args.cycle = 1025u;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_CYCLE, &args, &result);
    expect_true("seek cycle rts", result.focus.history_id == id_rts);

    /* Window clamp: cycle before oldest → oldest insn, clamped. */
    memset(&args, 0, sizeof(args));
    args.cycle = 1u;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_CYCLE, &args, &result);
    expect_true("clamp low id", result.focus.history_id == id_jsr);
    expect_true("clamp low flag", result.clamped);

    memset(&args, 0, sizeof(args));
    args.cycle = 99999u;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_CYCLE, &args, &result);
    expect_true("clamp high id", result.focus.history_id == id_other);
    expect_true("clamp high flag", result.clamped);

    memset(&args, 0, sizeof(args));
    args.history_id = window.newest_id + 50u;
    query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
    expect_true("clamp id high", result.focus.history_id == window.newest_id);
    expect_true("clamp id flag", result.clamped);

    /* Stale epoch seek rejected; focus unchanged. */
    {
        uint64_t keep = result.focus.history_id;
        memset(&args, 0, sizeof(args));
        args.epoch = window.epoch + 1u;
        args.history_id = id_jsr;
        expect_true(
            "epoch reject",
            runtime_tm_query(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result) ==
                RUNTIME_TM_QUERY_EPOCH_MISMATCH);
        expect_true("epoch focus kept", result.focus.history_id == keep);
    }

    /* RESET_COMPLETE is a walk barrier. */
    {
        uint64_t id_pre;
        uint64_t id_post;
        runtime_destroy(rt);
        rt = make_runtime();
        id_pre = emit_insn(rt->history, 2000u, 0x1000u, 0xF0u, 0xEAu);
        expect_true(
            "reset marker",
            runtime_history_append_marker(
                rt->history,
                RUNTIME_HISTORY_MARKER_RESET_COMPLETE,
                RUNTIME_HISTORY_RESET_EXPLICIT,
                0u,
                2010u));
        id_post = emit_insn(rt->history, 2020u, 0xFF69u, 0xFDu, 0xA9u);
        memset(&args, 0, sizeof(args));
        args.history_id = id_pre;
        query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
        memset(&args, 0, sizeof(args));
        args.direction = 1;
        expect_true(
            "step across reset",
            runtime_tm_query(rt, RUNTIME_TM_QUERY_STEP, &args, &result) ==
                RUNTIME_TM_QUERY_EPOCH_MISMATCH);
        expect_true("reset focus kept", result.focus.history_id == id_pre);
        (void)id_post;
    }

    /* History clear bumps epoch; old epoch seek rejected. */
    {
        uint64_t old_epoch;
        uint64_t new_id;
        runtime_history_status st;

        runtime_destroy(rt);
        rt = make_runtime();
        (void)emit_insn(rt->history, 3000u, 0x2000u, 0xF0u, 0xEAu);
        runtime_history_get_status(rt->history, &st);
        old_epoch = st.epoch;
        expect_true("clear", runtime_history_clear(rt->history, 4000u));
        new_id = emit_insn(rt->history, 4010u, 0x2000u, 0xF0u, 0xEAu);
        memset(&args, 0, sizeof(args));
        args.epoch = old_epoch;
        args.history_id = 1u;
        expect_true(
            "cleared epoch reject",
            runtime_tm_query(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result) ==
                RUNTIME_TM_QUERY_EPOCH_MISMATCH);
        memset(&args, 0, sizeof(args));
        args.history_id = new_id;
        query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
        expect_true("new epoch id", result.focus.history_id == new_id);
        expect_true("new epoch", result.focus.epoch != old_epoch);
    }

    /* JSR near stack wrap bails honestly. */
    {
        runtime_destroy(rt);
        rt = make_runtime();
        (void)emit_insn(rt->history, 5000u, 0x0300u, 0x00u, 0x20u);
        (void)emit_insn(rt->history, 5010u, 0x0306u, 0xFEu, 0xA9u);
        memset(&args, 0, sizeof(args));
        args.history_id = 1u;
        query_ok(rt, RUNTIME_TM_QUERY_SEEK_ID, &args, &result);
        expect_true(
            "sp wrap",
            runtime_tm_query(rt, RUNTIME_TM_QUERY_STEP_OVER, NULL, &result) ==
                RUNTIME_TM_QUERY_SP_WRAP);
    }

    runtime_destroy(rt);

    /* Client/worker round-trip: command → TM_FOCUS event. Does not write apple2. */
    {
        runtime_config config;
        runtime_client *client;
        runtime_event event;
        uint64_t token;

        if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
            fprintf(stderr, "FAIL: SDL_Init\n");
            return 1;
        }
        runtime_config_init(&config);
        config.start_running = false;
        config.history_memory_mb = 16;
        config.history_memory_mb_configured = true;
        config.frame_ring_memory_mb = 0;
        config.frame_ring_memory_mb_configured = true;
        rt = runtime_create(&config);
        expect_true("client create", rt != NULL);
        expect_true("client start", runtime_start(rt));
        client = runtime_get_client(rt);
        expect_true("client", client != NULL);
        {
            clock_t start = clock();
            int paused = 0;
            while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
                while (runtime_client_poll_event(client, &event)) {
                    if (event.type == RUNTIME_EVENT_ERROR) {
                        fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                        exit(1);
                    }
                    if (event.type == RUNTIME_EVENT_PAUSED) {
                        paused = 1;
                    }
                }
                if (paused) {
                    break;
                }
                SDL_Delay(1);
            }
            expect_true("client paused", paused);
        }
        token = runtime_client_alloc_request_token(client);
        expect_true(
            "client query",
            runtime_client_tm_seek_cycle(client, 0u, token));
        expect_true("client event", wait_tm_focus(client, token, &event, 2.0));
        expect_true(
            "client empty or ok",
            event.data.tm_focus.status == RUNTIME_TM_QUERY_EMPTY ||
                event.data.tm_focus.status == RUNTIME_TM_QUERY_OK ||
                event.data.tm_focus.status == RUNTIME_TM_QUERY_END_OF_TAPE);
        runtime_stop(rt);
        runtime_destroy(rt);
        SDL_Quit();
    }

    printf("ok\n");
    return 0;
}

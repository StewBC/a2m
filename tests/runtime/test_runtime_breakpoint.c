#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s: expected true\n", name);
        exit(1);
    }
}

static int poll_event(
    runtime_client *client,
    runtime_event *event,
    runtime_event_type type,
    double timeout_s)
{
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
    }
    return 0;
}

static int wait_bp_count(runtime_client *client, uint16_t want, double timeout_s)
{
    runtime_event event;
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
            if (event.type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE &&
                event.data.breakpoints.count == want) {
                return 1;
            }
        }
    }
    return 0;
}

static void wait_paused(runtime_client *client)
{
    runtime_event event;
    expect_true("PAUSED", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));
}

static void drain_events(runtime_client *client, double timeout_s)
{
    runtime_event event;
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        if (!runtime_client_poll_event(client, &event)) {
            break;
        }
    }
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint16_t target_pc;
    uint16_t hit_pc;
    runtime_breakpoint_definition def;
    uint32_t created_id = 0;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init failed");
    }

    runtime_config_init(&config);
    config.start_running = false;
    config.slot_cards[5] = RUNTIME_SLOT_CARD_DISKII;
    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);

    expect_true("STARTED", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    wait_paused(client);
    expect_true("request cpu", runtime_client_request_cpu_state(client));
    expect_true(
        "CPU",
        poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    target_pc = event.data.cpu_state.pc;

    /* --- SET_EXECUTE free-run hit (legacy path) --- */
    expect_true(
        "set_bp",
        runtime_client_set_execute_breakpoint(client, target_pc));
    expect_true("bp count 1", wait_bp_count(client, 1u, 2.0));

    expect_true("run", runtime_client_run(client));
    expect_true("RUNNING", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
    expect_true("hit PAUSED", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));

    {
        clock_t start = clock();
        hit_pc = 0xFFFF;
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_CPU_STATE_RESPONSE) {
                    hit_pc = event.data.cpu_state.pc;
                }
            }
            if (hit_pc != 0xFFFF) {
                break;
            }
        }
    }
    if (hit_pc != target_pc) {
        fprintf(stderr, "FAIL: hit pc %04x expected %04x\n", hit_pc, target_pc);
        exit(1);
    }

    expect_true("clear_all", runtime_client_clear_all_breakpoints(client));
    expect_true("count 0", wait_bp_count(client, 0u, 2.0));

    /*
     * After a hit, suppress_execute_bp is set so the next free-run steps past
     * the current PC. Step once (no BPs) to clear suppress, then re-arm PC.
     */
    expect_true("step clear suppress", runtime_client_step_instruction(client));
    wait_paused(client);
    expect_true("restore pc", runtime_client_set_pc(client, target_pc));
    drain_events(client, 0.05);

    /* --- CREATE path (Misc Apply product surface) --- */
    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = target_pc;
    def.end_address = target_pc;
    def.has_end_address = 0u;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.mapping = 0u;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;

    expect_true("create_bp", runtime_client_create_breakpoint(client, &def));
    expect_true("create count 1", wait_bp_count(client, 1u, 2.0));

    /* Snapshot fields for UI list */
    {
        runtime_breakpoint_snapshot snap;
        clock_t start = clock();
        int got = 0;
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            if (runtime_client_poll_breakpoints(client, &snap) && snap.count >= 1u) {
                created_id = snap.entries[0].id;
                expect_true("snap enabled", snap.entries[0].enabled != 0);
                expect_true(
                    "snap access X",
                    (snap.entries[0].access & RUNTIME_BREAKPOINT_ACCESS_EXECUTE) != 0);
                expect_true(
                    "snap addr",
                    snap.entries[0].start_address == target_pc);
                got = 1;
                break;
            }
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE &&
                    event.data.breakpoints.count >= 1u) {
                    created_id = event.data.breakpoints.entries[0].id;
                    got = 1;
                }
            }
            if (got) {
                break;
            }
        }
        expect_true("snapshot after create", got);
        expect_true("created id", created_id != 0u);
    }

    expect_true("disable", runtime_client_set_breakpoint_enabled(client, created_id, false));
    {
        clock_t start = clock();
        int disabled = 0;
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE &&
                    event.data.breakpoints.count == 1u &&
                    event.data.breakpoints.entries[0].enabled == 0u) {
                    disabled = 1;
                    break;
                }
            }
            if (disabled) {
                break;
            }
        }
        expect_true("disabled in list", disabled);
    }

    expect_true("enable", runtime_client_set_breakpoint_enabled(client, created_id, true));
    expect_true("re-enable list", wait_bp_count(client, 1u, 2.0));
    drain_events(client, 0.05);

    /* Free-run should hit CREATE path BP */
    expect_true("run2", runtime_client_run(client));
    expect_true("RUNNING2", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
    expect_true("hit2 PAUSED", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));

    expect_true("clear id", runtime_client_clear_breakpoint(client, created_id));
    expect_true("cleared", wait_bp_count(client, 0u, 2.0));

    /* --- UPDATE + REARM command smoke (table mutation only) --- */
    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = target_pc;
    def.end_address = (uint16_t)(target_pc + 0x10u);
    def.has_end_address = 1u;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.mapping = 0u;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
    def.use_counter = 1u;
    def.initial_count = 1u;
    def.reset_count = 0u; /* oneshot */

    expect_true("create oneshot", runtime_client_create_breakpoint(client, &def));
    {
        runtime_breakpoint_definition upd;
        uint32_t id = 0;
        clock_t start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_BREAKPOINTS_RESPONSE &&
                    event.data.breakpoints.count == 1u) {
                    id = event.data.breakpoints.entries[0].id;
                    expect_true(
                        "range flag",
                        event.data.breakpoints.entries[0].has_end_address != 0);
                }
            }
            if (id != 0u) {
                break;
            }
        }
        expect_true("oneshot id", id != 0u);
        memset(&upd, 0, sizeof(upd));
        upd = def;
        upd.enabled = 0u;
        expect_true("update", runtime_client_update_breakpoint(client, id, &upd));
        expect_true("rearm", runtime_client_rearm_oneshot_breakpoints(client));
        expect_true("request after update", runtime_client_request_breakpoints(client));
        expect_true("still one", wait_bp_count(client, 1u, 2.0));
    }

    expect_true("clear all final", runtime_client_clear_all_breakpoints(client));
    expect_true("empty final", wait_bp_count(client, 0u, 2.0));

    /* Clear suppress_execute_bp from prior free-run hits (step with no BPs). */
    expect_true("step before map tests", runtime_client_step_instruction(client));
    wait_paused(client);

    /* --- Mapping filter: ROM at reset PC fires; AUX at same PC does not --- */
    {
        expect_true("pc rom for map", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        vf_set_d000(&def.mapping, A2SELD000_ROM);
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create rom map bp", runtime_client_create_breakpoint(client, &def));
        expect_true("rom bp listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run rom map", runtime_client_run(client));
        expect_true("running rom", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("rom map hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));
        expect_true("clear rom map", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after rom", wait_bp_count(client, 0u, 2.0));

        expect_true("step after rom", runtime_client_step_instruction(client));
        wait_paused(client);
        expect_true("pc rom again", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        vf_set_ram(&def.mapping, A2SEL48K_AUX);
        vf_set_d000(&def.mapping, A2SELD000_LC_B1);
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create aux map bp", runtime_client_create_breakpoint(client, &def));
        expect_true("aux listed", wait_bp_count(client, 1u, 2.0));
        /* Bounded run: AUX filter must not match ROM-mapped reset PC. */
        expect_true(
            "run cycles aux",
            runtime_client_run_cycles(client, 5000u));
        expect_true(
            "aux no bp stop",
            poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 5.0));
        expect_true("clear aux", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after aux", wait_bp_count(client, 0u, 2.0));
    }

    /* --- C100 Map/ROM distinction: slot ROM does not satisfy internal ROM. --- */
    {
        const uint16_t code = 0x0640u;
        const uint8_t slot_prog[] = {
            0xADu, 0x00u, 0xC6u,       /* LDA $C600 (slot 6 ROM) */
            0x4Cu, 0x40u, 0x06u        /* JMP $0640 */
        };
        const uint8_t internal_prog[] = {
            0xA9u, 0x00u,              /* LDA #0 */
            0x8Du, 0x07u, 0xC0u,       /* STA $C007 (SETCXROM) */
            0xADu, 0x00u, 0xC6u,       /* LDA $C600 (internal //e ROM) */
            0x4Cu, 0x45u, 0x06u        /* JMP $0645 */
        };
        size_t pi;

        for (pi = 0; pi < sizeof(slot_prog); ++pi) {
            expect_true(
                "poke C100 slot prog",
                runtime_client_write_memory_byte(
                    client, (uint16_t)(code + pi), slot_prog[pi], RUNTIME_MEMORY_MODE_MAIN));
        }
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = 0xC600u;
        def.end_address = 0xC600u;
        def.access = RUNTIME_BREAKPOINT_ACCESS_READ;
        vf_set_c100(&def.mapping, A2SELC100_ROM);
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create C100 ROM bp", runtime_client_create_breakpoint(client, &def));
        expect_true("C100 ROM bp listed", wait_bp_count(client, 1u, 2.0));
        expect_true("pc to slot ROM prog", runtime_client_set_pc(client, code));
        drain_events(client, 0.05);
        expect_true("run slot ROM", runtime_client_run_cycles(client, 1000u));
        expect_true(
            "slot ROM is not C100 internal ROM",
            poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 5.0));

        for (pi = 0; pi < sizeof(internal_prog); ++pi) {
            expect_true(
                "poke C100 internal prog",
                runtime_client_write_memory_byte(
                    client, (uint16_t)(code + pi), internal_prog[pi], RUNTIME_MEMORY_MODE_MAIN));
        }
        expect_true("pc to internal ROM prog", runtime_client_set_pc(client, code));
        drain_events(client, 0.05);
        expect_true("run internal ROM", runtime_client_run(client));
        expect_true("running internal ROM", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("C100 internal ROM hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));
        expect_true("clear C100 ROM bp", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after C100 ROM", wait_bp_count(client, 0u, 2.0));
    }

    /* --- WRITE watchpoint (P3 bus hook): CPU STA must pause --- */
    {
        const uint16_t code = 0x0600u;
        const uint16_t watch = 0x0300u;
        /* LDA #$42 / STA $0300 / JMP $0600 */
        const uint8_t prog[] = { 0xA9u, 0x42u, 0x8Du, 0x00u, 0x03u, 0x4Cu, 0x00u, 0x06u };
        size_t pi;

        for (pi = 0; pi < sizeof(prog); ++pi) {
            expect_true(
                "poke prog",
                runtime_client_write_memory_byte(
                    client,
                    (uint16_t)(code + pi),
                    prog[pi],
                    RUNTIME_MEMORY_MODE_MAP));
        }
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = watch;
        def.end_address = watch;
        def.access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create write bp", runtime_client_create_breakpoint(client, &def));
        expect_true("write bp listed", wait_bp_count(client, 1u, 2.0));
        expect_true("pc to code", runtime_client_set_pc(client, code));
        drain_events(client, 0.05);
        expect_true("run write", runtime_client_run(client));
        expect_true("running write", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("write hit PAUSED", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));
        expect_true("clear write bps", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after write", wait_bp_count(client, 0u, 2.0));
    }

    /* --- Composite mapping: WRITE uses the write map (RAMWRT), not read map. --- */
    {
        const uint16_t code = 0x0620u;
        const uint16_t watch = 0x0320u;
        /* LDA #0 / STA $C005 (RAMWRT aux) / STA $0320 / STA $C004 (main) / JMP $0620 */
        const uint8_t prog[] = {
            0xA9u, 0x00u, 0x8Du, 0x05u, 0xC0u, 0x8Du, 0x20u, 0x03u,
            0x8Du, 0x04u, 0xC0u, 0x4Cu, 0x20u, 0x06u
        };
        size_t pi;

        expect_true("step before write-map test", runtime_client_step_instruction(client));
        wait_paused(client);
        for (pi = 0; pi < sizeof(prog); ++pi) {
            expect_true(
                "poke write-map prog",
                runtime_client_write_memory_byte(
                    client,
                    (uint16_t)(code + pi),
                    prog[pi],
                    RUNTIME_MEMORY_MODE_MAIN));
        }

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = watch;
        def.end_address = watch;
        def.access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
        vf_set_ram(&def.mapping, A2SEL48K_MAIN);
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create main write-map bp", runtime_client_create_breakpoint(client, &def));
        expect_true("main write-map listed", wait_bp_count(client, 1u, 2.0));
        expect_true("pc to write-map code", runtime_client_set_pc(client, code));
        drain_events(client, 0.05);
        expect_true("run main write-map", runtime_client_run_cycles(client, 1000u));
        expect_true(
            "main filter ignores aux write",
            poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 5.0));
        expect_true("clear main write-map", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after main write-map", wait_bp_count(client, 0u, 2.0));

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = watch;
        def.end_address = watch;
        def.access = RUNTIME_BREAKPOINT_ACCESS_WRITE;
        vf_set_ram(&def.mapping, A2SEL48K_AUX);
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create aux write-map bp", runtime_client_create_breakpoint(client, &def));
        expect_true("aux write-map listed", wait_bp_count(client, 1u, 2.0));
        expect_true("pc to aux write-map code", runtime_client_set_pc(client, code));
        drain_events(client, 0.05);
        expect_true("run aux write-map", runtime_client_run(client));
        expect_true("running aux write-map", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("aux write-map hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));
        expect_true("clear aux write-map", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after aux write-map", wait_bp_count(client, 0u, 2.0));
        /* Execute the following STA $C004 so later host writes use main again. */
        expect_true("step RAMWRT cleanup", runtime_client_step_instruction(client));
        wait_paused(client);
    }

    /* --- FAST / SLOW actions (max / 1 MHz) --- */
    {
        uint32_t turbo = 0xFFFFFFFFu;
        clock_t start;

        expect_true("step before turbo acts", runtime_client_step_instruction(client));
        wait_paused(client);
        expect_true("pc for fast", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_FAST;
        expect_true("create fast bp", runtime_client_create_breakpoint(client, &def));
        expect_true("fast listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run fast", runtime_client_run(client));
        expect_true("running fast", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("fast hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));

        turbo = 0xFFFFFFFFu;
        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                    turbo = event.data.machine_state.active_turbo_multiplier;
                }
            }
            if (turbo == RUNTIME_TURBO_MAX) {
                break;
            }
            (void)runtime_client_request_machine_state(client);
        }
        if (turbo != RUNTIME_TURBO_MAX) {
            fprintf(stderr, "FAIL: FAST expected turbo max (0), got %u\n", turbo);
            exit(1);
        }

        expect_true("clear fast", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after fast", wait_bp_count(client, 0u, 2.0));
        expect_true("step after fast", runtime_client_step_instruction(client));
        wait_paused(client);
        expect_true("pc for slow", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_SLOW;
        expect_true("create slow bp", runtime_client_create_breakpoint(client, &def));
        expect_true("slow listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run slow", runtime_client_run(client));
        expect_true("running slow", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("slow hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));

        turbo = 0xFFFFFFFFu;
        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                    turbo = event.data.machine_state.active_turbo_multiplier;
                }
            }
            if (turbo == RUNTIME_TURBO_MHZ_1) {
                break;
            }
            (void)runtime_client_request_machine_state(client);
        }
        if (turbo != RUNTIME_TURBO_MHZ_1) {
            fprintf(stderr, "FAIL: SLOW expected turbo 1 MHz (1000), got %u\n", turbo);
            exit(1);
        }
        expect_true("clear slow", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after slow", wait_bp_count(client, 0u, 2.0));
    }

    /* --- Inverted range must not match whole memory (was wrap-map bug) --- */
    {
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = 0xC800u;
        def.end_address = 0xC7FEu;
        def.has_end_address = 1u;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK;
        expect_true("create inverted range", runtime_client_create_breakpoint(client, &def));
        expect_true("inv listed", wait_bp_count(client, 1u, 2.0));
        /* PC in ROM far from C7FE..C800 must not hit. */
        expect_true("pc far", runtime_client_set_pc(client, 0xE239u));
        drain_events(client, 0.05);
        expect_true("run cycles far", runtime_client_run_cycles(client, 2000u));
        expect_true(
            "no wrap hit",
            poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 5.0));
        expect_true("clear inv", runtime_client_clear_all_breakpoints(client));
        expect_true("empty inv", wait_bp_count(client, 0u, 2.0));
        expect_true("step after inv", runtime_client_step_instruction(client));
        wait_paused(client);
    }

    /* --- TYPE action: starts Apple paste; does not change turbo --- */
    {
        uint32_t turbo_before = 0xFFFFFFFFu;
        uint32_t turbo_after = 0xFFFFFFFFu;
        clock_t start;

        expect_true("step before type", runtime_client_step_instruction(client));
        wait_paused(client);
        /* Leave turbo at 1 MHz (SLOW above) and confirm TYPE does not mutate it. */
        expect_true("set turbo 1 for type", runtime_client_set_turbo_multiplier(client, RUNTIME_TURBO_MHZ_1));
        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                    turbo_before = event.data.machine_state.active_turbo_multiplier;
                }
            }
            if (turbo_before == RUNTIME_TURBO_MHZ_1) {
                break;
            }
            (void)runtime_client_request_machine_state(client);
        }
        expect_true("pc for type", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_TYPE;
        snprintf(def.type_text, sizeof(def.type_text), "%s", "HELLO");
        expect_true("create type bp", runtime_client_create_breakpoint(client, &def));
        expect_true("type listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run type", runtime_client_run(client));
        expect_true("running type", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));
        expect_true("type hit", poll_event(client, &event, RUNTIME_EVENT_PAUSED, 5.0));

        turbo_after = 0xFFFFFFFFu;
        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                    turbo_after = event.data.machine_state.active_turbo_multiplier;
                }
            }
            if (turbo_after != 0xFFFFFFFFu) {
                break;
            }
            (void)runtime_client_request_machine_state(client);
        }
        if (turbo_after != RUNTIME_TURBO_MHZ_1) {
            fprintf(
                stderr,
                "FAIL: TYPE must not change turbo (want 1000, got %u)\n",
                turbo_after);
            exit(1);
        }
        expect_true("clear type", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after type", wait_bp_count(client, 0u, 2.0));
    }

    /* --- SWAP action: emits DISK_SWAP (queue step; empty queue is still event) --- */
    {
        bool saw_swap = false;
        bool saw_paused = false;
        clock_t start;

        expect_true("step before swap", runtime_client_step_instruction(client));
        wait_paused(client);
        expect_true("pc for swap", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);

        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_SWAP;
        def.swap_slot = 5u;
        def.swap_param = 0; /* bare → next */
        def.swap_relative = 0;
        expect_true("create swap bp", runtime_client_create_breakpoint(client, &def));
        expect_true("swap listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run swap", runtime_client_run(client));
        expect_true("running swap", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));

        /* DISK_SWAP is published before PAUSED; watch for both. */
        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 5.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR) {
                    fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                    exit(1);
                }
                if (event.type == RUNTIME_EVENT_DISK_SWAP &&
                    event.data.disk_swap.slot == 5u &&
                    event.data.disk_swap.device == 0u &&
                    event.data.disk_swap.swap_param == 1 &&
                    event.data.disk_swap.swap_relative != 0u) {
                    saw_swap = true;
                }
                if (event.type == RUNTIME_EVENT_PAUSED) {
                    saw_paused = true;
                }
            }
            if (saw_swap && saw_paused) {
                break;
            }
        }
        if (!saw_swap) {
            fprintf(stderr, "FAIL: SWAP expected DISK_SWAP event (drive 0, +1)\n");
            exit(1);
        }
        if (!saw_paused) {
            fprintf(stderr, "FAIL: SWAP expected PAUSED after hit\n");
            exit(1);
        }
        expect_true("clear swap", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after swap", wait_bp_count(client, 0u, 2.0));
    }

    /* A syntactically valid slot without Disk II logs an error and pauses,
       even when the breakpoint did not request the BREAK action. */
    {
        bool saw_error = false;
        bool saw_paused = false;
        clock_t start;

        expect_true("step before invalid swap", runtime_client_step_instruction(client));
        wait_paused(client);
        expect_true("pc for invalid swap slot", runtime_client_set_pc(client, target_pc));
        drain_events(client, 0.05);
        memset(&def, 0, sizeof(def));
        def.enabled = 1u;
        def.start_address = target_pc;
        def.end_address = target_pc;
        def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
        def.mapping = 0u;
        def.actions = RUNTIME_BREAKPOINT_ACTION_SWAP;
        def.swap_slot = 0u;
        expect_true("create invalid-slot swap bp", runtime_client_create_breakpoint(client, &def));
        expect_true("invalid-slot swap listed", wait_bp_count(client, 1u, 2.0));
        expect_true("run invalid-slot swap", runtime_client_run(client));
        expect_true("running invalid-slot swap", poll_event(client, &event, RUNTIME_EVENT_RUNNING, 2.0));

        start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 5.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_ERROR &&
                    strstr(event.data.error.message, "slot 0 does not contain a Disk II") != NULL) {
                    saw_error = true;
                }
                if (event.type == RUNTIME_EVENT_PAUSED) {
                    saw_paused = true;
                }
            }
            if (saw_error && saw_paused) {
                break;
            }
        }
        expect_true("invalid swap slot error", saw_error);
        expect_true("invalid swap slot pauses", saw_paused);
        expect_true("clear invalid swap", runtime_client_clear_all_breakpoints(client));
        expect_true("empty after invalid swap", wait_bp_count(client, 0u, 2.0));
    }

    expect_true("quit", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

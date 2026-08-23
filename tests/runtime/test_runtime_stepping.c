#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef A2M_FIXTURE_DIR
#define A2M_FIXTURE_DIR "tests/fixtures"
#endif

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

static void drain_events(runtime_client *client)
{
    runtime_event event;
    while (runtime_client_poll_event(client, &event)) {
        if (event.type == RUNTIME_EVENT_ERROR) {
            fprintf(stderr, "runtime error: %s\n", event.data.error.message);
            exit(1);
        }
    }
}

static int poll_machine_without_reset(
    runtime_client *client, runtime_event *event, double timeout_s)
{
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR ||
                event->type == RUNTIME_EVENT_RESET_COMPLETE) {
                return 0;
            }
            if (event->type == RUNTIME_EVENT_MACHINE_STATE_RESPONSE) {
                return 1;
            }
        }
    }
    return 0;
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    char disk_path[1024];
    uint16_t pc_before;
    uint16_t pc_after;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init failed");
    }

    runtime_config_init(&config);
    {
        int slot;
        for (slot = 1; slot <= 7; ++slot) {
            config.slot_cards[slot] = RUNTIME_SLOT_CARD_EMPTY;
        }
        config.slot_cards[1] = RUNTIME_SLOT_CARD_DISKII;
        config.slot_cards[2] = RUNTIME_SLOT_CARD_SMARTPORT;
        config.slot_cards[7] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
    }
    /* Default ROMs; do not free-run — stay paused for stepping. */
    config.start_running = false;

    rt = runtime_create(&config);
    expect_true("runtime_create", rt != NULL);
    expect_true("runtime_start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    expect_true("STARTED", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    expect_true(
        "RESET_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE, 2.0));
    expect_true(
        "CPU_STATE",
        poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    pc_before = event.data.cpu_state.pc;
    drain_events(client);

    expect_true("request machine slots", runtime_client_request_machine_state(client));
    expect_true("machine slots", poll_event(
        client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    expect_true("Disk II slot 1",
        event.data.machine_state.slots[1].card_type == RUNTIME_SLOT_CARD_DISKII);
    expect_true("SmartPort slot 2",
        event.data.machine_state.slots[2].card_type == RUNTIME_SLOT_CARD_SMARTPORT);
    expect_true("Mockingboard slot 7",
        event.data.machine_state.slots[7].card_type == RUNTIME_SLOT_CARD_MOCKINGBOARD);

    snprintf(disk_path, sizeof(disk_path),
        "%s/Apple DOS 3.3 January 1983.nib", A2M_FIXTURE_DIR);

    expect_true("insert Disk II 1", runtime_client_media_insert(
        client, 1, 0, RUNTIME_SLOT_CARD_DISKII, disk_path));
    expect_true("Disk II insert event", poll_event(
        client, &event, RUNTIME_EVENT_MEDIA_CHANGED, 2.0));
    expect_true("Disk II insert success", event.data.media_changed.success != 0u);
    expect_true("insert Disk II 2", runtime_client_media_insert(
        client, 1, 0, RUNTIME_SLOT_CARD_DISKII, disk_path));
    expect_true("Disk II second insert event", poll_event(
        client, &event, RUNTIME_EVENT_MEDIA_CHANGED, 2.0));
    expect_true("swap Disk II", runtime_client_media_swap(client, 1, 0, 1, true));
    expect_true("Disk II swap event", poll_event(
        client, &event, RUNTIME_EVENT_MEDIA_CHANGED, 2.0));
    expect_true("Disk II swap success", event.data.media_changed.success != 0u);
    expect_true("request Disk II queue", runtime_client_request_machine_state(client));
    expect_true("Disk II queue state", poll_event(
        client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
    expect_true("Disk II queue count",
        event.data.machine_state.slots[1].devices[0].queue_count == 2u);
    expect_true("Disk II queue wrapped",
        event.data.machine_state.slots[1].devices[0].queue_index == 0u);

    {
        const char *sp_path = "test_runtime_smartport.po";
        FILE *file = fopen(sp_path, "wb");
        uint8_t block[512] = {0};
        int i;
        expect_true("create SmartPort image", file != NULL);
        for (i = 0; i < 4; ++i) {
            expect_true("write SmartPort image", fwrite(block, 1, sizeof(block), file) == sizeof(block));
        }
        fclose(file);
        expect_true("insert SmartPort", runtime_client_media_insert(
            client, 2, 0, RUNTIME_SLOT_CARD_SMARTPORT, sp_path));
        expect_true("SmartPort insert event", poll_event(
            client, &event, RUNTIME_EVENT_MEDIA_CHANGED, 2.0));
        expect_true("SmartPort insert success", event.data.media_changed.success != 0u);
        expect_true("eject SmartPort", runtime_client_media_eject(client, 2, 0));
        expect_true("SmartPort eject event", poll_event(
            client, &event, RUNTIME_EVENT_MEDIA_CHANGED, 2.0));
        expect_true("SmartPort eject success", event.data.media_changed.success != 0u);
        remove(sp_path);
    }

    expect_true("step_instruction", runtime_client_step_instruction(client));
    expect_true(
        "STEP_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE, 2.0));
    pc_after = event.data.step_complete.cpu.pc;
    if (pc_after == pc_before) {
        /* Self-loop is rare at reset; still require cycles advanced. */
        expect_true(
            "cycles advanced",
            event.data.step_complete.cpu.cycles > 0);
    }

    expect_true("request_cpu", runtime_client_request_cpu_state(client));
    expect_true(
        "CPU after step",
        poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));

    expect_true("run_cycles", runtime_client_run_cycles(client, 100));
    expect_true(
        "RUN_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_RUN_COMPLETE, 2.0));

    drain_events(client);
    expect_true("boot slot 1", runtime_client_boot_slot(client, 1));
    {
        clock_t start = clock();
        int saw_c100 = 0;
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 2.0) {
            while (runtime_client_poll_event(client, &event)) {
                if (event.type == RUNTIME_EVENT_CPU_STATE_RESPONSE &&
                    event.data.cpu_state.pc == 0xC100u) {
                    saw_c100 = 1;
                    break;
                }
            }
            if (saw_c100) {
                break;
            }
        }
        expect_true("boot PC C100", saw_c100);
    }

    {
        runtime_machine_config machine_config = {0};
        machine_config.apple_model = 0u;
        machine_config.slot_cards[1] = RUNTIME_SLOT_CARD_DISKII;
        machine_config.slot_cards[2] = RUNTIME_SLOT_CARD_SMARTPORT;
        machine_config.slot_cards[7] = RUNTIME_SLOT_CARD_MOCKINGBOARD;

        drain_events(client);
        expect_true("queue unchanged machine config",
            runtime_client_apply_machine_config(
                client, &machine_config, NULL, NULL, NULL,
                true, false, false));
        expect_true("request state after unchanged config",
            runtime_client_request_machine_state(client));
        expect_true("unchanged config does not reset",
            poll_machine_without_reset(client, &event, 2.0));

        machine_config.apple_model = 1u;
        machine_config.slot_cards[1] = RUNTIME_SLOT_CARD_EMPTY;
        machine_config.slot_cards[3] = RUNTIME_SLOT_CARD_DISKII;
        machine_config.slot_cards[7] = RUNTIME_SLOT_CARD_EMPTY;
        machine_config.slot_cards[4] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
        expect_true("queue changed machine config",
            runtime_client_apply_machine_config(
                client, &machine_config, NULL, NULL, NULL,
                true, false, false));
        expect_true("changed config power-cycle", poll_event(
            client, &event, RUNTIME_EVENT_RESET_COMPLETE, 2.0));
        expect_true("changed config machine state", poll_event(
            client, &event, RUNTIME_EVENT_MACHINE_STATE_RESPONSE, 2.0));
        expect_true("changed model applied", event.data.machine_state.apple_model == 1u);
        expect_true("removed slot visible",
            event.data.machine_state.slots[1].card_type == RUNTIME_SLOT_CARD_EMPTY);
        expect_true("new Disk II visible",
            event.data.machine_state.slots[3].card_type == RUNTIME_SLOT_CARD_DISKII);
        expect_true("moved Mockingboard visible",
            event.data.machine_state.slots[4].card_type == RUNTIME_SLOT_CARD_MOCKINGBOARD &&
            event.data.machine_state.slots[7].card_type == RUNTIME_SLOT_CARD_EMPTY);
    }

    expect_true("quit", runtime_client_quit(client));
    expect_true("STOPPED", poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0));

    runtime_stop(rt);
    runtime_destroy(rt);

    runtime_config_init(&config);
    config.slot_cards[3] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
    config.slot_cards[4] = RUNTIME_SLOT_CARD_MOCKINGBOARD;
    expect_true("reject multiple Mockingboards", runtime_create(&config) == NULL);

    SDL_Quit();
    printf("ok\n");
    return 0;
}

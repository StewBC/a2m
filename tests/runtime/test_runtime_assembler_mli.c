#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "../test_file.h"

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

static uint16_t read_pc(runtime_client *client)
{
    runtime_event event;

    if (!runtime_client_request_cpu_state(client)) {
        fail("request cpu");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0)) {
        fail("CPU_STATE_RESPONSE");
    }
    return event.data.cpu_state.pc;
}

static void poke_bf00(runtime_client *client, uint8_t value)
{
    if (!runtime_client_write_memory_byte(
            client, 0xBF00u, value, RUNTIME_MEMORY_MODE_MAP)) {
        fail("write $BF00");
    }
}

static void pause_machine(runtime_client *client)
{
    runtime_event event;

    if (!runtime_client_pause(client)) {
        fail("pause");
    }
    (void)poll_event(client, &event, RUNTIME_EVENT_PAUSED, 2.0);
}

static void assemble_mli(
    runtime_client *client,
    const char *path,
    bool mli_launch,
    bool reset_first,
    runtime_event *out_complete)
{
    runtime_event event;

    if (!runtime_client_assemble_file_full(
            client,
            path,
            0x3000u,
            0x3000u,
            true, /* auto_run */
            mli_launch,
            reset_first,
            false)) {
        fail("assemble_file_full");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_ASSEMBLE_COMPLETE, 5.0)) {
        fail("ASSEMBLE_COMPLETE");
    }
    if (out_complete != NULL) {
        *out_complete = event;
    }
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    char path[160];
    uint16_t pc_after;
    /* Tight loop so PC stays at the run address after auto-run. */
    const char *source =
        "* = $3000\n"
        "loop:\n"
        "    jmp loop\n";

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init");
    }

    if (a2m_test_write_temp_file(path, sizeof(path), "a2m_rt_asm_mli", source) != 0) {
        fail("temp file");
    }

    runtime_config_init(&config);
    config.start_running = false;
    rt = runtime_create(&config);
    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime start");
    }
    client = runtime_get_client(rt);

    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0)) {
        fail("STARTED");
    }
    (void)poll_event(client, &event, RUNTIME_EVENT_PAUSED, 2.0);

    /* A: $BF00 == $4C → assemble OK, auto-run applied */
    poke_bf00(client, 0x4Cu);
    assemble_mli(client, path, true, false, &event);
    if (strstr(event.data.assemble.notice, "MLI launch skipped") != NULL) {
        fail("A: unexpected MLI skip notice");
    }
    pause_machine(client);
    pc_after = read_pc(client);
    if (pc_after != 0x3000u) {
        fprintf(stderr, "A: PC=$%04X expected $3000\n", pc_after);
        fail("A: auto-run PC");
    }

    /* B: $BF00 != $4C → assemble OK, auto-run skipped, notice set */
    poke_bf00(client, 0x00u);
    if (!runtime_client_set_pc(client, 0x1234u)) {
        fail("set PC");
    }
    /* Allow the set-register command to complete before reading. */
    {
        clock_t start = clock();
        while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < 0.05) {
            runtime_event discard;
            while (runtime_client_poll_event(client, &discard)) {
            }
        }
    }
    if (read_pc(client) != 0x1234u) {
        fail("B: setup PC");
    }
    assemble_mli(client, path, true, false, &event);
    if (strstr(event.data.assemble.notice, "MLI launch skipped") == NULL) {
        fprintf(stderr, "B: notice='%s'\n", event.data.assemble.notice);
        fail("B: expected MLI skip notice");
    }
    pc_after = read_pc(client);
    if (pc_after == 0x3000u) {
        fail("B: auto-run should not set PC");
    }
    if (pc_after != 0x1234u) {
        fprintf(stderr, "B: PC=$%04X expected $1234\n", pc_after);
        fail("B: PC should be unchanged");
    }

    /* C: mli_launch wins over stale reset_first — still auto-runs when MLI present */
    poke_bf00(client, 0x4Cu);
    assemble_mli(client, path, true, true, &event);
    if (strstr(event.data.assemble.notice, "MLI launch skipped") != NULL) {
        fail("C: unexpected MLI skip notice");
    }
    pause_machine(client);
    pc_after = read_pc(client);
    if (pc_after != 0x3000u) {
        fprintf(stderr, "C: PC=$%04X expected $3000\n", pc_after);
        fail("C: mli_launch should still auto-run");
    }

    (void)runtime_client_quit(client);
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);
    a2m_test_remove_file(path);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

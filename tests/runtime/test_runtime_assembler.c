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

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    char path[128];
    char targets_path[128];
    char invalid_path[128];
    const char *source =
        "* = $3000\n"
        "    lda #$42\n"
        "    sta $0300\n"
        "    stz $0301\n"
        "    rts\n";

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init");
    }

    if (a2m_test_write_temp_file(path, sizeof(path), "a2m_rt_asm", source) != 0) {
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

    if (!runtime_client_assemble_file(client, path, 0x3000)) {
        fail("assemble request");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_ASSEMBLE_COMPLETE, 5.0)) {
        fail("ASSEMBLE_COMPLETE");
    }
    if (event.data.assemble.address != 0x3000u ||
        strcmp(event.data.assemble.path, path) != 0) {
        fail("assemble meta");
    }

    if (!runtime_client_request_memory(client, 0x3000, 8, RUNTIME_MEMORY_MODE_MAP)) {
        fail("request memory");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0)) {
        fail("MEMORY_RESPONSE");
    }
    if (event.data.memory.bytes[0] != 0xA9 || event.data.memory.bytes[1] != 0x42) {
        fprintf(
            stderr,
            "bytes: %02X %02X\n",
            event.data.memory.bytes[0],
            event.data.memory.bytes[1]);
        fail("lda not present");
    }

    {
        const char *targets_source =
            ".if AM65 .eq 0\n"
            "    .if APPLE2 .eq 1\n"
            "        .scope mapped file=\"ignored.bin\"\n"
            "            .org $3100\n"
            "            .byte $a2\n"
            "        .endscope\n"
            "    .endif\n"
            ".endif\n"
            ".scope auxiliary dest=\"aux\"\n"
            "    .org $3200\n"
            "    .byte $a8\n"
            ".endscope\n"
            ".scope language_card dest=\"lc2\"\n"
            "    .org $d000\n"
            "    .byte $c2\n"
            ".endscope\n"
            ".scope combined dest=\"aux,lc2\"\n"
            "    .org $3300\n"
            "    .byte $ac\n"
            ".endscope\n";

        if (a2m_test_write_temp_file(
                targets_path,
                sizeof(targets_path),
                "a2m_rt_asm_targets",
                targets_source) != 0) {
            fail("target temp file");
        }
        if (!runtime_client_assemble_file(client, targets_path, 0x3000)) {
            fail("target assemble request");
        }
        if (!poll_event(client, &event, RUNTIME_EVENT_ASSEMBLE_COMPLETE, 5.0)) {
            fail("target ASSEMBLE_COMPLETE");
        }

        if (!runtime_client_request_memory(client, 0x3100, 1, RUNTIME_MEMORY_MODE_MAP) ||
            !poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0) ||
            event.data.memory.bytes[0] != 0xA2) {
            fail("file-only scope did not default to map");
        }
        if (!runtime_client_request_memory(client, 0x3200, 1, RUNTIME_MEMORY_MODE_AUX) ||
            !poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0) ||
            event.data.memory.bytes[0] != 0xA8) {
            fail("aux destination");
        }
        if (!runtime_client_request_memory(client, 0xD000, 1, RUNTIME_MEMORY_MODE_LC2) ||
            !poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0) ||
            event.data.memory.bytes[0] != 0xC2) {
            fail("lc2 destination");
        }
        if (!runtime_client_request_memory(client, 0x3300, 1, RUNTIME_MEMORY_MODE_AUX) ||
            !poll_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE, 2.0) ||
            event.data.memory.bytes[0] != 0xAC) {
            fail("combined aux,lc2 destination");
        }
    }

    {
        const char *invalid_source =
            ".scope obsolete dest=\"6502\"\n"
            "    .byte $ff\n"
            ".endscope\n";

        if (a2m_test_write_temp_file(
                invalid_path,
                sizeof(invalid_path),
                "a2m_rt_asm_invalid_dest",
                invalid_source) != 0) {
            fail("invalid target temp file");
        }
        if (!runtime_client_assemble_file(client, invalid_path, 0x3000)) {
            fail("invalid target assemble request");
        }
        if (!poll_event(client, &event, RUNTIME_EVENT_ASSEMBLE_ERROR, 5.0)) {
            fail("6502 destination should be rejected");
        }
    }

    (void)runtime_client_quit(client);
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);

    /* The ][+ selects the portable 6502 profile, so the same undirected STZ
       source must be rejected unless it explicitly opts into .65c02. */
    runtime_config_init(&config);
    config.start_running = false;
    config.apple_model = 1;
    rt = runtime_create(&config);
    if (rt == NULL || !runtime_start(rt)) {
        fail("runtime ][+ start");
    }
    client = runtime_get_client(rt);
    if (!poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0)) {
        fail("][+ STARTED");
    }
    (void)poll_event(client, &event, RUNTIME_EVENT_PAUSED, 2.0);
    if (!runtime_client_assemble_file(client, path, 0x3000)) {
        fail("][+ assemble request");
    }
    if (!poll_event(client, &event, RUNTIME_EVENT_ASSEMBLE_ERROR, 5.0)) {
        fail("][+ should reject 65C02 opcode");
    }
    (void)runtime_client_quit(client);
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);

    a2m_test_remove_file(path);
    a2m_test_remove_file(targets_path);
    a2m_test_remove_file(invalid_path);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

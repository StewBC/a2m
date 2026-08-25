/* Stop-path CRT: Override dumps RAM; beam buffer keeps a mid-frame raster. */
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"
#include "softswitch.h"
#include "video.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {
    FRAME_PIXELS = APPLE2_VIDEO_WIDTH * APPLE2_VIDEO_HEIGHT,
    HGR_PAGE_SIZE = 0x2000
};

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

static int count_nonblack(const uint32_t *fb, size_t n)
{
    size_t i;
    int c = 0;
    for (i = 0; i < n; i++) {
        if ((fb[i] & 0x00FFFFFFu) != 0u) {
            c++;
        }
    }
    return c;
}

static int poll_event(
    runtime_client *client,
    runtime_event *event,
    runtime_event_type type,
    Uint32 timeout_ms)
{
    Uint32 start = SDL_GetTicks();
    while ((SDL_GetTicks() - start) < timeout_ms) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) {
                return 1;
            }
        }
        SDL_Delay(1);
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

static void drain_frames(runtime_client *client, uint32_t *pixels)
{
    uint32_t w;
    uint32_t h;
    uint64_t fn;
    while (runtime_client_poll_argb_frame(
               client, pixels, FRAME_PIXELS, &w, &h, &fn)) {
    }
}

static int wait_frame(
    runtime_client *client,
    uint32_t *pixels,
    Uint32 timeout_ms)
{
    Uint32 start = SDL_GetTicks();
    uint32_t w;
    uint32_t h;
    uint64_t fn;
    runtime_event event;
    while ((SDL_GetTicks() - start) < timeout_ms) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
        }
        if (runtime_client_poll_argb_frame(
                client, pixels, FRAME_PIXELS, &w, &h, &fn)) {
            return 1;
        }
        SDL_Delay(1);
    }
    return 0;
}

static void settle(runtime_client *client)
{
    runtime_event event;
    Uint32 start = SDL_GetTicks();
    while ((SDL_GetTicks() - start) < 50u) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR) {
                fprintf(
                    stderr, "runtime error: %s\n", event.data.error.message);
                exit(1);
            }
        }
        SDL_Delay(1);
    }
}

static void fill_main(
    runtime_client *client,
    uint32_t *pixels,
    uint16_t address,
    uint16_t length,
    uint8_t value)
{
    uint8_t chunk[RUNTIME_MEMORY_SNAPSHOT_MAX];
    uint16_t off = 0;
    memset(chunk, value, sizeof(chunk));
    while (off < length) {
        uint16_t n = (uint16_t)(length - off);
        if (n > RUNTIME_MEMORY_SNAPSHOT_MAX) {
            n = RUNTIME_MEMORY_SNAPSHOT_MAX;
        }
        expect_true(
            "write hgr",
            runtime_client_write_memory(
                client,
                (uint16_t)(address + off),
                n,
                RUNTIME_MEMORY_MODE_MAIN,
                chunk));
        off = (uint16_t)(off + n);
    }
    /* Each paused write publishes a CRT frame; drop them before the next assert. */
    settle(client);
    drain_frames(client, pixels);
}

static void step_nop(runtime_client *client)
{
    uint8_t nop = 0xEAu;
    runtime_event event;
    expect_true(
        "write nop",
        runtime_client_write_memory(
            client, 0x0300, 1, RUNTIME_MEMORY_MODE_MAP, &nop));
    expect_true("set pc nop", runtime_client_set_pc(client, 0x0300));
    settle(client);
    expect_true("step nop", runtime_client_step_instruction(client));
    expect_true(
        "STEP_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE, 2000u));
}

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint32_t *pixels;
    int nonblack;
    uint8_t hgr_on[] = {
        0x8D, 0x50, 0xC0, /* STA $C050 TEXT off */
        0x8D, 0x57, 0xC0  /* STA $C057 HIRES on */
    };

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fail("SDL_Init failed");
    }

    pixels = (uint32_t *)malloc((size_t)FRAME_PIXELS * sizeof(uint32_t));
    expect_true("pixels", pixels != NULL);

    runtime_config_init(&config);
    config.start_running = false;

    rt = runtime_create(&config);
    expect_true("runtime_create", rt != NULL);
    expect_true("runtime_start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    expect_true("STARTED", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2000u));
    expect_true(
        "RESET_COMPLETE",
        poll_event(client, &event, RUNTIME_EVENT_RESET_COMPLETE, 2000u));
    drain_events(client);
    drain_frames(client, pixels);

    /* --- Override on: stop dumps RAM (hidden page), not the beam buffer. --- */
    fill_main(client, pixels, 0x4000, HGR_PAGE_SIZE, 0x7Fu);
    expect_true(
        "override page2 hgr",
        runtime_client_set_display_override(client, true, A2S_HIRES | A2S_PAGE2));
    expect_true("override painted", wait_frame(client, pixels, 2000u));
    nonblack = count_nonblack(pixels, FRAME_PIXELS);
    if (nonblack < 1000) {
        fprintf(stderr, "FAIL: override fill nonblack=%d\n", nonblack);
        exit(1);
    }

    fill_main(client, pixels, 0x4000, HGR_PAGE_SIZE, 0x00u);
    step_nop(client);
    expect_true("override stop frame", wait_frame(client, pixels, 2000u));
    nonblack = count_nonblack(pixels, FRAME_PIXELS);
    if (nonblack != 0) {
        fprintf(
            stderr,
            "FAIL: override stop should dump cleared RAM, nonblack=%d\n",
            nonblack);
        exit(1);
    }

    /* --- Override off: stop publishes the beam buffer, not a RAM dump. --- */
    expect_true(
        "override off",
        runtime_client_set_display_override(client, false, 0u));
    expect_true("override-off frame", wait_frame(client, pixels, 2000u));

    fill_main(client, pixels, 0x2000, HGR_PAGE_SIZE, 0x7Fu);
    expect_true(
        "write hgr switches",
        runtime_client_write_memory(
            client, 0x0300, (uint16_t)sizeof(hgr_on), RUNTIME_MEMORY_MODE_MAP, hgr_on));
    expect_true("set pc hgr", runtime_client_set_pc(client, 0x0300));
    settle(client);
    drain_frames(client, pixels);
    expect_true("step TEXT off", runtime_client_step_instruction(client));
    expect_true(
        "STEP TEXT off",
        poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE, 2000u));
    expect_true("step HIRES on", runtime_client_step_instruction(client));
    expect_true(
        "STEP HIRES on",
        poll_event(client, &event, RUNTIME_EVENT_STEP_COMPLETE, 2000u));
    drain_events(client);
    drain_frames(client, pixels);

    expect_true(
        "dump hgr",
        runtime_client_set_display_override(client, true, A2S_HIRES));
    expect_true("hgr dump frame", wait_frame(client, pixels, 2000u));
    expect_true(
        "dump off",
        runtime_client_set_display_override(client, false, 0u));
    expect_true("hgr beam seed", wait_frame(client, pixels, 2000u));
    nonblack = count_nonblack(pixels, FRAME_PIXELS);
    if (nonblack < 1000) {
        fprintf(stderr, "FAIL: hgr seed nonblack=%d\n", nonblack);
        exit(1);
    }

    /* Stop with Override off keeps the beam when video RAM is not rewritten. */
    drain_frames(client, pixels);
    step_nop(client);
    expect_true("beam stop frame", wait_frame(client, pixels, 2000u));
    nonblack = count_nonblack(pixels, FRAME_PIXELS);
    if (nonblack < 1000) {
        fprintf(
            stderr,
            "FAIL: beam stop should keep raster, nonblack=%d\n",
            nonblack);
        exit(1);
    }

    /* Paused memory writes always dump video RAM onto the CRT. */
    {
        uint8_t chunk[RUNTIME_MEMORY_SNAPSHOT_MAX];
        uint16_t off = 0;
        memset(chunk, 0, sizeof(chunk));
        drain_frames(client, pixels);
        while (off < HGR_PAGE_SIZE) {
            uint16_t n = (uint16_t)(HGR_PAGE_SIZE - off);
            if (n > RUNTIME_MEMORY_SNAPSHOT_MAX) {
                n = RUNTIME_MEMORY_SNAPSHOT_MAX;
            }
            expect_true(
                "clear hgr chunk",
                runtime_client_write_memory(
                    client,
                    (uint16_t)(0x2000u + off),
                    n,
                    RUNTIME_MEMORY_MODE_MAIN,
                    chunk));
            off = (uint16_t)(off + n);
        }
        expect_true("memory-edit refresh frame", wait_frame(client, pixels, 2000u));
        /* Consume any earlier chunk frames; keep the latest. */
        {
            uint32_t w;
            uint32_t h;
            uint64_t fn;
            while (runtime_client_poll_argb_frame(
                       client, pixels, FRAME_PIXELS, &w, &h, &fn)) {
            }
        }
        nonblack = count_nonblack(pixels, FRAME_PIXELS);
        if (nonblack != 0) {
            fprintf(
                stderr,
                "FAIL: paused HGR clear should refresh CRT, nonblack=%d\n",
                nonblack);
            exit(1);
        }
    }

    expect_true("quit", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2000u);
    runtime_stop(rt);
    runtime_destroy(rt);
    free(pixels);
    SDL_Quit();
    printf("OK runtime_display_stop\n");
    return 0;
}

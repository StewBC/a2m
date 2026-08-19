#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void expect_true(const char *name, int value)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static int poll_event(
    runtime_client *client,
    runtime_event *event,
    runtime_event_type type,
    double timeout_seconds)
{
    clock_t start = clock();
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_seconds) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                return 0;
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
    const char *path = "test_runtime_smartport_boot.po";
    uint8_t block[512] = {0};
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    FILE *file;
    int i;

    expect_true("SDL init", SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) == 0);
    file = fopen(path, "wb");
    expect_true("create SmartPort image", file != NULL);
    for (i = 0; i < 4; ++i) {
        expect_true("write SmartPort image",
            fwrite(block, 1, sizeof(block), file) == sizeof(block));
    }
    expect_true("close SmartPort image", fclose(file) == 0);

    runtime_config_init(&config);
    for (i = 1; i <= 7; ++i) {
        config.slot_cards[i] = RUNTIME_SLOT_CARD_EMPTY;
    }
    config.apple_model = 1;
    config.slot_cards[5] = RUNTIME_SLOT_CARD_SMARTPORT;
    config.smartport_mounts[0].slot = 5;
    config.smartport_mounts[0].unit = 0;
    config.smartport_mounts[0].path = path;
    config.smartport_mount_count = 1;
    config.smartport_boot_slot = 5;
    config.start_running = false;

    rt = runtime_create(&config);
    expect_true("create runtime", rt != NULL);
    expect_true("start runtime", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("runtime client", client != NULL);
    expect_true("startup CPU state",
        poll_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE, 2.0));
    expect_true("SmartPort boot PC is C500", event.data.cpu_state.pc == 0xC500u);

    expect_true("quit runtime", runtime_client_quit(client));
    expect_true("runtime stopped",
        poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0));
    runtime_stop(rt);
    runtime_destroy(rt);
    expect_true("remove SmartPort image", remove(path) == 0);

    runtime_config_init(&config);
    config.smartport_boot_slot = 8;
    expect_true("reject invalid boot slot", runtime_create(&config) == NULL);

    SDL_Quit();
    printf("ok runtime SmartPort boot\n");
    return 0;
}

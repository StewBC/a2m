#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void expect(int condition, const char *message)
{
    if (!condition) fail(message);
}

static int wait_event(runtime_client *client, runtime_event *event, runtime_event_type type)
{
    clock_t start = clock();
    while ((double)(clock() - start) / CLOCKS_PER_SEC < 3.0) {
        while (runtime_client_poll_event(client, event)) {
            if (event->type == RUNTIME_EVENT_ERROR) {
                fprintf(stderr, "runtime error: %s\n", event->data.error.message);
                exit(1);
            }
            if (event->type == type) return 1;
        }
    }
    return 0;
}

static void sync_worker(runtime_client *client)
{
    runtime_event event;
    expect(runtime_client_request_cpu_state(client), "queue worker sync");
    expect(wait_event(client, &event, RUNTIME_EVENT_CPU_STATE_RESPONSE), "worker sync");
}

static void write_bytes(const char *path, const void *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    expect(file != NULL, "open test input");
    expect(fwrite(bytes, 1, size, file) == size, "write test input");
    fclose(file);
}

static uint8_t *read_bytes(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    expect(file != NULL, "open test output");
    expect(fseek(file, 0, SEEK_END) == 0, "seek test output");
    length = ftell(file);
    expect(length >= 0 && fseek(file, 0, SEEK_SET) == 0, "size test output");
    bytes = (uint8_t *)malloc((size_t)length + 1u);
    expect(bytes != NULL, "allocate test output");
    expect(fread(bytes, 1, (size_t)length, file) == (size_t)length, "read test output");
    fclose(file);
    bytes[length] = 0u;
    *out_size = (size_t)length;
    return bytes;
}

int main(void)
{
    static const char listing[] = "20 GOTO 10\n10 PRINT \"HI\"\n";
    static const char expected_listing[] = "10 PRINT \"HI\"\n20 GOTO 10\n";
    static const uint8_t raw[] = {0xa9u, 0x2au, 0x60u};
    const char *listing_in = "test_machine_files_in.bas";
    const char *listing_out = "test_machine_files_out.bas";
    const char *raw_in = "test_machine_files_in.bin";
    const char *naps_base = "test_machine_files_out.bin";
    const char *naps_out = "test_machine_files_out.bin#062000";
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    uint8_t *saved;
    size_t saved_size;

    write_bytes(listing_in, listing, sizeof(listing) - 1u);
    write_bytes(raw_in, raw, sizeof(raw));
    expect(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) == 0, "SDL init");
    runtime_config_init(&config);
    config.start_running = false;
    rt = runtime_create(&config);
    expect(rt != NULL && runtime_start(rt), "runtime start");
    client = runtime_get_client(rt);
    expect(wait_event(client, &event, RUNTIME_EVENT_STARTED), "runtime started");

    expect(runtime_client_load_bin(
        client, listing_in, 0u, APPLE2_BINARY_FORMAT_AUTO, false, true, false),
        "queue Applesoft import");
    expect(runtime_client_request_memory(
        client, 0x0067u, 8u, RUNTIME_MEMORY_MODE_MAIN), "request Applesoft pointers");
    expect(wait_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE), "Applesoft pointer response");
    expect(event.data.memory.bytes[0] == 0x01u && event.data.memory.bytes[1] == 0x08u,
        "TXTTAB points at $0801");
    expect(event.data.memory.bytes[2] > 0x01u || event.data.memory.bytes[3] > 0x08u,
        "VARTAB follows program");

    expect(runtime_client_save_bin(
        client, listing_out, 0u, 0u, APPLE2_BINARY_FORMAT_RAW, true),
        "queue Applesoft export");
    sync_worker(client);
    saved = read_bytes(listing_out, &saved_size);
    expect(saved_size == sizeof(expected_listing) - 1u &&
        memcmp(saved, expected_listing, saved_size) == 0, "Applesoft listing round trip");
    free(saved);

    expect(runtime_client_load_bin(
        client, raw_in, 0x2000u, APPLE2_BINARY_FORMAT_RAW, false, false, false),
        "queue raw load");
    expect(runtime_client_request_memory(
        client, 0x2000u, sizeof(raw), RUNTIME_MEMORY_MODE_MAP), "request raw bytes");
    expect(wait_event(client, &event, RUNTIME_EVENT_MEMORY_RESPONSE), "raw memory response");
    expect(memcmp(event.data.memory.bytes, raw, sizeof(raw)) == 0, "raw bytes loaded");

    expect(runtime_client_save_bin(
        client, naps_base, 0x2000u, 0x2002u, APPLE2_BINARY_FORMAT_NAPS, false),
        "queue NAPS save");
    sync_worker(client);
    saved = read_bytes(naps_out, &saved_size);
    expect(saved_size == sizeof(raw) && memcmp(saved, raw, sizeof(raw)) == 0,
        "NAPS save preserves raw data fork");
    free(saved);

    expect(runtime_client_quit(client), "queue quit");
    (void)wait_event(client, &event, RUNTIME_EVENT_STOPPED);
    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    remove(listing_in);
    remove(listing_out);
    remove(raw_in);
    remove(naps_out);
    puts("runtime machine file tests passed");
    return 0;
}

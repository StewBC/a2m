/*
 * P4e: breakpoint [DEBUG] break.* INI load + save round-trip.
 */
#include "config.h"
#include "runtime.h"
#include "runtime_client.h"
#include "runtime_event.h"

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

static void write_ini(const char *path, const char *body)
{
    FILE *f = fopen(path, "wb");
    expect_true("open ini for write", f != NULL);
    expect_true("write body", fputs(body, f) >= 0);
    fclose(f);
}

static int file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    int found;

    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (char *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[size] = '\0';
    fclose(f);
    found = strstr(buf, needle) != NULL;
    free(buf);
    return found;
}

int main(void)
{
    char ini_path[256];
    snprintf(
        ini_path,
        sizeof(ini_path),
        "a2m_bp_ini_test_%ld.tmp",
        (long)time(NULL));
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    runtime_event event;
    runtime_breakpoint_definition def;
    runtime_breakpoint_snapshot snap;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed\n");
        return 1;
    }

    /* Seed INI with one exec break at $E000 (map). */
    write_ini(
        ini_path,
        "[DEBUG]\n"
        "break.E000=execute,map,break\n"
        "break.C000-C001=write,main,c100rom,lc1,break,swap-slot=5,swap=+1\n");

    runtime_config_init(&config);
    config.start_running = false;
    config.use_ini = true;
    config.save_ini = true;
    config.ini_path = ini_path;

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);

    expect_true("started", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    /* Load publishes breakpoints; also re-request for the RPC snapshot. */
    expect_true("req bps", runtime_client_request_breakpoints(client));
    expect_true("loaded 2", wait_bp_count(client, 2u, 2.0));
    expect_true("poll snap", runtime_client_poll_breakpoints(client, &snap));
    expect_true("snap count 2", snap.count == 2u);

    {
        int found_e000 = 0;
        int found_c000 = 0;
        uint16_t i;
        for (i = 0; i < snap.count; ++i) {
            if (snap.entries[i].start_address == 0xE000u &&
                !snap.entries[i].has_end_address &&
                (snap.entries[i].actions & RUNTIME_BREAKPOINT_ACTION_BREAK) != 0) {
                found_e000 = 1;
            }
            if (snap.entries[i].start_address == 0xC000u &&
                snap.entries[i].has_end_address &&
                snap.entries[i].end_address == 0xC001u &&
                vf_get_ram(snap.entries[i].mapping) == A2SEL48K_MAIN &&
                vf_get_c100(snap.entries[i].mapping) == A2SELC100_ROM &&
                vf_get_d000(snap.entries[i].mapping) == A2SELD000_LC_B1 &&
                (snap.entries[i].access & RUNTIME_BREAKPOINT_ACCESS_WRITE) != 0 &&
                (snap.entries[i].actions & RUNTIME_BREAKPOINT_ACTION_SWAP) != 0 &&
                snap.entries[i].swap_slot == 5u &&
                snap.entries[i].swap_param == 1 &&
                snap.entries[i].swap_relative != 0u) {
                found_c000 = 1;
            }
        }
        expect_true("loaded E000 exec", found_e000);
        expect_true("loaded composite mapping/swap+1", found_c000);
    }

    /* Add a third BP, stop, save, and confirm it lands in the INI. */
    memset(&def, 0, sizeof(def));
    def.enabled = 1u;
    def.start_address = 0x0801u;
    def.end_address = 0x0801u;
    def.access = RUNTIME_BREAKPOINT_ACCESS_EXECUTE;
    def.mapping = 0u;
    def.actions = RUNTIME_BREAKPOINT_ACTION_BREAK | RUNTIME_BREAKPOINT_ACTION_FAST;
    expect_true("create 0801", runtime_client_create_breakpoint(client, &def));
    expect_true("now 3", wait_bp_count(client, 3u, 2.0));

    expect_true("quit", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    expect_true("save debug ini", runtime_save_debug_ini(rt));
    runtime_destroy(rt);
    rt = NULL;

    expect_true("ini has E000", file_contains(ini_path, "break.E000"));
    expect_true("ini has C000-C001", file_contains(ini_path, "break.C000-C001"));
    expect_true("ini has 0801", file_contains(ini_path, "break.0801"));
    expect_true("ini has fast", file_contains(ini_path, "fast"));
    expect_true("ini has swap=+1", file_contains(ini_path, "swap=+1"));
    expect_true("ini has swap-slot=5", file_contains(ini_path, "swap-slot=5"));
    expect_true("ini has main", file_contains(ini_path, "main"));
    expect_true("ini has c100rom", file_contains(ini_path, "c100rom"));
    expect_true("ini has lc1", file_contains(ini_path, "lc1"));

    /* Cold load again from the saved file — count stays 3. */
    runtime_config_init(&config);
    config.start_running = false;
    config.use_ini = true;
    config.save_ini = false;
    config.ini_path = ini_path;
    rt = runtime_create(&config);
    expect_true("create2", rt != NULL);
    expect_true("start2", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("started2", poll_event(client, &event, RUNTIME_EVENT_STARTED, 2.0));
    expect_true("req2", runtime_client_request_breakpoints(client));
    expect_true("reload 3", wait_bp_count(client, 3u, 2.0));
    expect_true("quit2", runtime_client_quit(client));
    (void)poll_event(client, &event, RUNTIME_EVENT_STOPPED, 2.0);
    runtime_stop(rt);
    runtime_destroy(rt);

    (void)remove(ini_path);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

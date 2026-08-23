/* TM3: materialize into live apple2_t, enter/exit NOW, read-only, control. */
#include "apple2.h"
#include "apple2_snapshot.h"
#include "control_dispatch.h"
#include "control_protocol.h"
#include "control_server.h"
#include "diskii.h"
#include "mboard.h"
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

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void drain_ignore_error(runtime_client *client)
{
    runtime_event event;
    while (runtime_client_poll_event(client, &event)) {
    }
}

static int wait_event_type(
    runtime_client *client, runtime_event_type type, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == type) {
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static int wait_tm_mode(
    runtime_client *client,
    uint64_t token,
    runtime_event *out,
    int *saw_reason,
    runtime_state_changed_reason want_reason,
    double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    int got_mode = 0;
    if (saw_reason != NULL) {
        *saw_reason = 0;
    }
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_STATE_CHANGED &&
                event.data.state_changed.reason == want_reason) {
                if (saw_reason != NULL) {
                    *saw_reason = 1;
                }
            }
            if (event.type == RUNTIME_EVENT_TM_MODE &&
                event.request_token == token) {
                if (out != NULL) {
                    *out = event;
                }
                got_mode = 1;
            }
        }
        if (got_mode) {
            return 1;
        }
        SDL_Delay(1);
    }
    return 0;
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

static int wait_error_code(
    runtime_client *client, const char *code, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_ERROR &&
                strcmp(event.data.error.code, code) == 0) {
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

static int wait_cpu(
    runtime_client *client, runtime_cpu_snapshot *out, double timeout_s)
{
    clock_t start = clock();
    runtime_event event;
    expect_true("req cpu", runtime_client_request_cpu_state(client));
    while ((double)(clock() - start) / (double)CLOCKS_PER_SEC < timeout_s) {
        while (runtime_client_poll_event(client, &event)) {
            if (event.type == RUNTIME_EVENT_CPU_STATE_RESPONSE) {
                if (out != NULL) {
                    *out = event.data.cpu_state;
                }
                return 1;
            }
        }
        SDL_Delay(1);
    }
    return 0;
}

typedef struct pump_ctx {
    runtime_client *client;
    control_dispatch_t *disp;
    SDL_atomic_t alive;
} pump_ctx;

static int pump_thread_main(void *userdata)
{
    pump_ctx *ctx = (pump_ctx *)userdata;
    runtime_event event;
    while (SDL_AtomicGet(&ctx->alive)) {
        while (runtime_client_poll_event(ctx->client, &event)) {
            control_dispatch_on_runtime_event(ctx->disp, &event);
        }
        control_dispatch_poll(ctx->disp);
        control_dispatch_check_session(ctx->disp);
        SDL_Delay(1);
    }
    return 0;
}

#ifndef _WIN32
static int tcp_cmd(int fd, const char *req, char *resp, size_t resp_size)
{
    char buf[1024];
    size_t len = strlen(req);
    ssize_t n;
    size_t got = 0;

    if (send(fd, req, len, 0) != (ssize_t)len) {
        return 0;
    }
    for (;;) {
        n = recv(fd, buf + got, sizeof(buf) - 1u - got, 0);
        if (n <= 0) {
            return 0;
        }
        got += (size_t)n;
        buf[got] = '\0';
        if (strchr(buf, '\n') != NULL) {
            /* Skip unsolicited event lines (id 0). */
            char *line = buf;
            while (strncmp(line, "0 ", 2) == 0) {
                char *nl = strchr(line, '\n');
                if (nl == NULL) {
                    break;
                }
                line = nl + 1;
            }
            if (line[0] != '\0') {
                strncpy(resp, line, resp_size - 1u);
                resp[resp_size - 1u] = '\0';
                {
                    char *nl = strchr(resp, '\n');
                    if (nl != NULL) {
                        nl[1] = '\0';
                    }
                }
                return 1;
            }
        }
        if (got >= sizeof(buf) - 1u) {
            return 0;
        }
    }
}

static int tcp_connect_port(uint16_t port)
{
    int fd;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}
#endif

int main(void)
{
    runtime_config config;
    runtime *rt;
    runtime_client *client;
    uint64_t token;
    runtime_event ev;
    int saw_reason = 0;
    runtime_cpu_snapshot cpu_now;
    runtime_cpu_snapshot cpu_then;
    runtime_cpu_snapshot cpu_back;
    uint16_t pc_now;
    uint64_t cycles_now;
    uint8_t ram_now;
    uint8_t motor_now;
    uint16_t via_t1_now;
    runtime_tm_window window;
    apple2_t scratch;
    uint16_t seek_pc;
    uint8_t seek_a;
    uint8_t seek_ram;

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "FAIL: SDL_Init\n");
        return 1;
    }

    runtime_config_init(&config);
    config.start_running = false;
    config.timemachine = true;
    config.timemachine_memory_mb = 16;
    config.timemachine_memory_mb_configured = true;
    config.history_memory_mb = 16;
    config.history_memory_mb_configured = true;
    config.frame_ring_memory_mb = 8;
    config.frame_ring_memory_mb_configured = true;
    expect_true("turbo 1", runtime_config_set_turbo_csv(&config, "1"));

    rt = runtime_create(&config);
    expect_true("create", rt != NULL);
    expect_true("start", runtime_start(rt));
    client = runtime_get_client(rt);
    expect_true("client", client != NULL);
    expect_true("started", wait_event_type(client, RUNTIME_EVENT_STARTED, 2.0));
    expect_true("latched pause", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain_ignore_error(client);

    expect_true("run", runtime_client_run(client));
    expect_true("running", wait_event_type(client, RUNTIME_EVENT_RUNNING, 2.0));
    SDL_Delay(120);
    expect_true("pause", runtime_client_pause(client));
    expect_true("paused", wait_event_type(client, RUNTIME_EVENT_PAUSED, 2.0));
    drain_ignore_error(client);

    expect_true("TM on", runtime_tm_enabled(rt));
    expect_true("has CP", runtime_tm_checkpoint_count(rt) >= 1u);
    runtime_tm_window_info(rt, &window);
    expect_true("window valid", window.valid);

    pc_now = rt->machine.cpu.cpu.pc;
    cycles_now = apple2_cycles(&rt->machine);
    ram_now = apple2_debug_read(&rt->machine, 0x0000);
    motor_now = rt->machine.diskii_controller[6].diskii_drive[0].motor_on;
    via_t1_now = rt->machine.mockingboard[4].via[0].t1_counter;

    /* Empty enter: TM off. */
    {
        uint64_t tok = runtime_client_alloc_request_token(client);
        expect_true("tm off", runtime_client_tm_set_enabled(client, false, tok));
        SDL_Delay(30);
        drain_ignore_error(client);
        tok = runtime_client_alloc_request_token(client);
        expect_true("enter off", runtime_client_tm_enter_forensic(client, tok));
        expect_true(
            "enter off event",
            wait_tm_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_OTHER, 2.0));
        expect_true(
            "enter unavailable",
            ev.data.tm_mode.status == RUNTIME_TM_ENTER_UNAVAILABLE);
        expect_true("still live", !runtime_tm_in_forensic(rt));
        tok = runtime_client_alloc_request_token(client);
        expect_true("tm on", runtime_client_tm_set_enabled(client, true, tok));
        SDL_Delay(30);
        drain_ignore_error(client);
        (void)runtime_tm_checkpoint_take(rt);
    }

    /* Recorder off (pin 3) → enter empty. */
    {
        uint64_t tok = runtime_client_alloc_request_token(client);
        expect_true("hist off", runtime_client_history_record(client, false, tok));
        SDL_Delay(30);
        drain_ignore_error(client);
        tok = runtime_client_alloc_request_token(client);
        expect_true("enter empty", runtime_client_tm_enter_forensic(client, tok));
        expect_true(
            "enter empty event",
            wait_tm_mode(
                client, tok, &ev, NULL, RUNTIME_STATE_CHANGED_OTHER, 2.0));
        expect_true(
            "enter empty status",
            ev.data.tm_mode.status == RUNTIME_TM_ENTER_EMPTY);
        tok = runtime_client_alloc_request_token(client);
        expect_true("hist on", runtime_client_history_record(client, true, tok));
        SDL_Delay(30);
        drain_ignore_error(client);
        (void)runtime_tm_checkpoint_take(rt);
    }

    token = runtime_client_alloc_request_token(client);
    expect_true("enter", runtime_client_tm_enter_forensic(client, token));
    expect_true(
        "enter event",
        wait_tm_mode(
            client,
            token,
            &ev,
            &saw_reason,
            RUNTIME_STATE_CHANGED_FORENSIC_ENTER,
            2.0));
    expect_true("enter ok", ev.data.tm_mode.status == RUNTIME_TM_ENTER_OK);
    expect_true("mode forensic", ev.data.tm_mode.mode == RUNTIME_TM_MODE_FORENSIC);
    expect_true("in forensic", runtime_tm_in_forensic(rt));
    expect_true("enter inform", saw_reason);

    expect_true("cpu after enter", wait_cpu(client, &cpu_then, 2.0));
    expect_true("sealed", rt->machine.replay_sealed);
    expect_true("paused forensic", rt->exec_state != RUNTIME_EXEC_RUNNING);

    /* Read-only: poke / live step. */
    expect_true(
        "poke",
        runtime_client_write_memory_byte(
            client, 0x0300, 0xA9, RUNTIME_MEMORY_MODE_MAIN));
    expect_true(
        "poke error",
        wait_error_code(client, RUNTIME_ERROR_READ_ONLY_FORENSIC, 2.0));
    expect_true("step live", runtime_client_step_instruction(client));
    expect_true(
        "step error",
        wait_error_code(client, RUNTIME_ERROR_READ_ONLY_FORENSIC, 2.0));
    expect_true("run live", runtime_client_run(client));
    expect_true(
        "run error",
        wait_error_code(client, RUNTIME_ERROR_READ_ONLY_FORENSIC, 2.0));
    expect_true("still forensic after poke", runtime_tm_in_forensic(rt));

    /* Seek to mid-window; live matches scratch materialize. */
    runtime_tm_window_info(rt, &window);
    expect_true("window after enter", window.valid);
    {
        uint64_t mid = window.oldest_cycle +
            (window.newest_cycle - window.oldest_cycle) / 2u;
        uint64_t focus_cycle;
        if (mid < window.oldest_cycle) {
            mid = window.oldest_cycle;
        }
        token = runtime_client_alloc_request_token(client);
        expect_true("seek mid", runtime_client_tm_seek_cycle(client, mid, token));
        expect_true("seek event", wait_tm_focus(client, token, &ev, 2.0));
        expect_true(
            "seek ok",
            ev.data.tm_focus.status == RUNTIME_TM_QUERY_OK);
        expect_true("seek focus", ev.data.tm_focus.focus.valid);
        focus_cycle = ev.data.tm_focus.focus.cycle;
        expect_true("init scratch", apple2_init(&scratch));
        expect_true(
            "scratch mat", runtime_tm_materialize(rt, focus_cycle, &scratch));
        seek_pc = scratch.cpu.cpu.pc;
        seek_a = scratch.cpu.cpu.A;
        seek_ram = apple2_debug_read(&scratch, 0x0000);
        apple2_shutdown(&scratch);

        expect_true("cpu after seek", wait_cpu(client, &cpu_back, 2.0));
        if (rt->machine.cpu.cpu.pc != seek_pc) {
            fprintf(
                stderr,
                "seek pc live=%04X scratch=%04X focus_cycle=%llu live_cyc=%llu\n",
                rt->machine.cpu.cpu.pc,
                seek_pc,
                (unsigned long long)focus_cycle,
                (unsigned long long)apple2_cycles(&rt->machine));
        }
        expect_true("seek pc", rt->machine.cpu.cpu.pc == seek_pc);
        expect_true("seek a", rt->machine.cpu.cpu.A == seek_a);
        expect_true(
            "seek ram", apple2_debug_read(&rt->machine, 0x0000) == seek_ram);
        expect_true("still sealed", rt->machine.replay_sealed);
    }

    /* Backward seek to oldest. */
    {
        uint64_t old = window.oldest_cycle;
        token = runtime_client_alloc_request_token(client);
        expect_true("seek old", runtime_client_tm_seek_cycle(client, old, token));
        expect_true("seek old event", wait_tm_focus(client, token, &ev, 2.0));
        expect_true(
            "seek old ok",
            ev.data.tm_focus.status == RUNTIME_TM_QUERY_OK);
        expect_true(
            "cycles moved or equal",
            apple2_cycles(&rt->machine) <= cycles_now);
    }

    /* Window-edge: seek outside does not move state. */
    {
        uint16_t pc_before = rt->machine.cpu.cpu.pc;
        uint64_t cyc_before = apple2_cycles(&rt->machine);
        token = runtime_client_alloc_request_token(client);
        expect_true(
            "seek outside",
            runtime_client_tm_seek_cycle(
                client, window.newest_cycle + 100000u, token));
        expect_true("outside event", wait_tm_focus(client, token, &ev, 2.0));
        expect_true(
            "outside not retained",
            ev.data.tm_focus.status == RUNTIME_TM_QUERY_NOT_RETAINED);
        expect_true("pc unchanged", rt->machine.cpu.cpu.pc == pc_before);
        expect_true("cyc unchanged", apple2_cycles(&rt->machine) == cyc_before);
    }

    /* Force materialize failure, then exit still restores NOW. */
    runtime_tm_on_history_invalidate(rt);
    expect_true("cps cleared", runtime_tm_checkpoint_count(rt) == 0u);
    token = runtime_client_alloc_request_token(client);
    expect_true(
        "seek after wipe",
        runtime_client_tm_seek_cycle(client, window.oldest_cycle, token));
    expect_true("wipe event", wait_tm_focus(client, token, &ev, 2.0));
    expect_true(
        "wipe failed or not retained",
        ev.data.tm_focus.status == RUNTIME_TM_QUERY_MATERIALIZE_FAILED ||
            ev.data.tm_focus.status == RUNTIME_TM_QUERY_NOT_RETAINED ||
            ev.data.tm_focus.status == RUNTIME_TM_QUERY_EMPTY);
    expect_true("still forensic after fail", runtime_tm_in_forensic(rt));

    token = runtime_client_alloc_request_token(client);
    expect_true("exit", runtime_client_tm_exit_forensic(client, token));
    expect_true(
        "exit event",
        wait_tm_mode(
            client,
            token,
            &ev,
            &saw_reason,
            RUNTIME_STATE_CHANGED_FORENSIC_EXIT,
            2.0));
    expect_true("exit ok", ev.data.tm_mode.status == RUNTIME_TM_ENTER_OK);
    expect_true("exit inform", saw_reason);
    expect_true("mode live", ev.data.tm_mode.mode == RUNTIME_TM_MODE_LIVE);
    expect_true("not forensic", !runtime_tm_in_forensic(rt));
    expect_true("unsealed", !rt->machine.replay_sealed);
    expect_true("still paused", rt->exec_state != RUNTIME_EXEC_RUNNING);
    expect_true("pc restored", rt->machine.cpu.cpu.pc == pc_now);
    expect_true("cycles restored", apple2_cycles(&rt->machine) == cycles_now);
    expect_true(
        "ram restored", apple2_debug_read(&rt->machine, 0x0000) == ram_now);
    expect_true(
        "disk motor restored",
        rt->machine.diskii_controller[6].diskii_drive[0].motor_on == motor_now);
    expect_true(
        "via t1 restored",
        rt->machine.mockingboard[4].via[0].t1_counter == via_t1_now);
    expect_true("cpu after exit", wait_cpu(client, &cpu_now, 2.0));
    expect_true("cpu pc now", cpu_now.pc == pc_now);

    /* Live poke works after exit. */
    drain_ignore_error(client);
    expect_true(
        "live poke",
        runtime_client_write_memory_byte(
            client, 0x0300, 0xEA, RUNTIME_MEMORY_MODE_MAIN));
    SDL_Delay(20);
    drain_ignore_error(client);

#ifndef _WIN32
    /* Control: status reports forensic; exit verb from a socket session. */
    {
        control_server_t *server = NULL;
        control_dispatch_t disp;
        pump_ctx pump;
        SDL_Thread *th = NULL;
        uint16_t port;
        int fd = -1;
        char resp[512];
        int i;

        port = (uint16_t)(18731u + ((unsigned)getpid() % 200u));
        for (i = 0; i < 16; ++i) {
            server = control_server_create((uint16_t)(port + i));
            if (server != NULL && control_server_start(server)) {
                port = (uint16_t)(port + i);
                break;
            }
            if (server != NULL) {
                control_server_destroy(server);
                server = NULL;
            }
        }
        expect_true("control server", server != NULL);
        control_dispatch_init(&disp, server, client);
        SDL_AtomicSet(&pump.alive, 1);
        pump.client = client;
        pump.disp = &disp;
        th = SDL_CreateThread(pump_thread_main, "tm3-pump", &pump);
        expect_true("pump thread", th != NULL);

        expect_true("run2", runtime_client_run(client));
        SDL_Delay(80);
        expect_true("pause2", runtime_client_pause(client));
        {
            clock_t t0 = clock();
            while (rt->exec_state == RUNTIME_EXEC_RUNNING &&
                (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
                SDL_Delay(1);
            }
        }
        expect_true("paused2", rt->exec_state != RUNTIME_EXEC_RUNNING);
        token = runtime_client_alloc_request_token(client);
        expect_true("enter2", runtime_client_tm_enter_forensic(client, token));
        {
            clock_t t0 = clock();
            while (!runtime_tm_in_forensic(rt) &&
                (double)(clock() - t0) / (double)CLOCKS_PER_SEC < 2.0) {
                SDL_Delay(1);
            }
        }
        if (!runtime_tm_in_forensic(rt)) {
            fprintf(stderr, "enter2 did not stick, can=%d\n",
                (int)runtime_tm_can_enter(rt));
        }
        expect_true("enter2 forensic", runtime_tm_in_forensic(rt));
        SDL_Delay(30);

        fd = tcp_connect_port(port);
        expect_true("tcp connect", fd >= 0);
        SDL_Delay(50);
        expect_true("hello", tcp_cmd(fd, "1 hello\n", resp, sizeof(resp)));
        expect_true("hello a2m12", strstr(resp, "A2M/12") != NULL);
        expect_true(
            "caps", tcp_cmd(fd, "2 capabilities\n", resp, sizeof(resp)));
        expect_true("caps tm", strstr(resp, "timemachine") != NULL);
        expect_true("get-state", tcp_cmd(fd, "3 get-state\n", resp, sizeof(resp)));
        if (strstr(resp, "mode=forensic") == NULL) {
            fprintf(stderr, "get-state: %s", resp);
        }
        expect_true("state forensic", strstr(resp, "mode=forensic") != NULL);
        expect_true("focus_cycle field", strstr(resp, "focus_cycle=") != NULL);
        expect_true(
            "set-mem blocked",
            tcp_cmd(fd, "4 set-reg pc $300\n", resp, sizeof(resp)));
        expect_true(
            "readonly code",
            strstr(resp, "read-only-forensic") != NULL);
        expect_true(
            "exit wire", tcp_cmd(fd, "5 exit-forensic\n", resp, sizeof(resp)));
        expect_true("exit accepted", strstr(resp, "ok") != NULL);
        SDL_Delay(80);
        expect_true(
            "get-state live", tcp_cmd(fd, "6 get-state\n", resp, sizeof(resp)));
        expect_true("state live", strstr(resp, "mode=live") != NULL);

        close(fd);
        SDL_AtomicSet(&pump.alive, 0);
        SDL_WaitThread(th, NULL);
        control_dispatch_shutdown(&disp);
        control_server_destroy(server);
        expect_true("exited via socket", !runtime_tm_in_forensic(rt));
    }
#endif

    runtime_stop(rt);
    runtime_destroy(rt);
    SDL_Quit();
    printf("ok\n");
    return 0;
}

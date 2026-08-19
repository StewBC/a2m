#include "control_breakpoint.h"
#include "control_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, bool value)
{
    if (!value) {
        fprintf(stderr, "%s: expected true\n", name);
        exit(1);
    }
}

static void expect_u32(const char *name, uint32_t expected, uint32_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %u, got %u\n", name, expected, actual);
        exit(1);
    }
}

static void expect_int(const char *name, int expected, int actual)
{
    if (expected != actual) {
        fprintf(stderr, "%s: expected %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

static void expect_string(const char *name, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "%s: expected `%s`, got `%s`\n", name, expected, actual);
        exit(1);
    }
}

int main(void)
{
    control_request request;
    control_response error;
    control_response response;
    char line[CONTROL_RESPONSE_TEXT_MAX];

    expect_true("hello", control_protocol_parse_request("1 hello\n", &request, &error));
    expect_u32("hello id", 1, request.id);
    expect_int("hello type", CONTROL_COMMAND_HELLO, (int)request.type);

    expect_true("get-cpu", control_protocol_parse_request("2 get-cpu", &request, &error));
    expect_int("get-cpu type", CONTROL_COMMAND_GET_CPU, (int)request.type);

    expect_true(
        "get-memory",
        control_protocol_parse_request("3 get-memory $300 16 map", &request, &error));
    expect_u32("addr", 0x300, request.args.address);
    expect_u32("len", 16, request.args.length);
    expect_u32("mode", CONTROL_MEMORY_MODE_MAP, request.args.memory_mode);

    expect_true(
        "get-memory aux",
        control_protocol_parse_request("4 get-memory 0x400 256 aux", &request, &error));
    expect_u32("mode aux", CONTROL_MEMORY_MODE_AUX, request.args.memory_mode);

    expect_true(
        "get-memory lc1",
        control_protocol_parse_request("14 get-memory $D000 16 lc1", &request, &error));
    expect_u32("mode lc1", CONTROL_MEMORY_MODE_LC1, request.args.memory_mode);

    expect_true(
        "break-exec",
        control_protocol_parse_request("5 break-exec $C000", &request, &error));
    expect_u32("bp", 0xC000, request.args.address);

    expect_true(
        "wait-paused",
        control_protocol_parse_request("6 wait-paused 5000", &request, &error));
    expect_u32("timeout", 5000, request.args.timeout_ms);

    expect_true(
        "wait-frame",
        control_protocol_parse_request("15 wait-frame 3 10000", &request, &error));
    expect_u32("frame delta", 3, request.args.wait_frame_delta);
    expect_u32("frame timeout", 10000, request.args.timeout_ms);

    expect_true(
        "wait-event",
        control_protocol_parse_request("16 wait-event step-complete 2000", &request, &error));
    expect_string("event name", "step-complete", request.args.event_name);

    expect_true(
        "set-turbo max",
        control_protocol_parse_request("17 set-turbo max", &request, &error));
    expect_u32("turbo max", 0, request.args.turbo_mode);
    expect_true(
        "set-turbo 4",
        control_protocol_parse_request("18 set-turbo 4", &request, &error));
    expect_u32("turbo 4MHz", 4000, request.args.turbo_mode);
    expect_true(
        "set-turbo -1",
        control_protocol_parse_request("19 set-turbo -1", &request, &error));
    expect_u32("turbo -1", 0, request.args.turbo_mode);

    expect_true(
        "get-state",
        control_protocol_parse_request("18 get-state", &request, &error));
    expect_int("get-state type", CONTROL_COMMAND_GET_STATE, (int)request.type);

    expect_true(
        "get-softswitches",
        control_protocol_parse_request("27 get-softswitches", &request, &error));
    expect_int(
        "get-softswitches type",
        CONTROL_COMMAND_GET_SOFTSWITCHES,
        (int)request.type);

    expect_true(
        "get-frame",
        control_protocol_parse_request("19 get-frame", &request, &error));
    expect_int("get-frame type", CONTROL_COMMAND_GET_FRAME, (int)request.type);

    expect_true(
        "step-over",
        control_protocol_parse_request("20 step-over", &request, &error));
    expect_int("step-over type", CONTROL_COMMAND_STEP_OVER, (int)request.type);

    expect_true(
        "save-state",
        control_protocol_parse_request("7 save-state /tmp/x.a2s", &request, &error));
    expect_string("path", "/tmp/x.a2s", request.args.path);

    expect_true(
        "set-reg",
        control_protocol_parse_request("8 set-reg pc $1234", &request, &error));
    expect_string("reg", "pc", request.args.reg_name);
    expect_u32("reg val", 0x1234, request.args.reg_value);

    expect_true(
        "key",
        control_protocol_parse_request("9 key 0x8D", &request, &error));
    expect_u32("key", 0x8D, request.args.key);

    expect_true(
        "bad unknown",
        !control_protocol_parse_request("10 foobar", &request, &error));

    control_protocol_format_ok(&response, 1, "protocol=" CONTROL_PROTOCOL_VERSION);
    expect_true(
        "fmt ok",
        control_protocol_write_response_line(line, sizeof(line), &response));
    expect_true(
        "break-create",
        control_protocol_parse_request(
            "21 break-create exec $C000 actions=break when=a==0",
            &request,
            &error));
    expect_int("break-create type", CONTROL_COMMAND_BREAK_CREATE, (int)request.type);
    expect_true("break-create text", strstr(request.args.text, "exec") != NULL);

    expect_true(
        "break-enable",
        control_protocol_parse_request("22 break-enable 3 0", &request, &error));
    expect_u32("enable id", 3, request.args.break_id);
    expect_u32("enable flag", 0, request.args.break_enable);

    expect_true(
        "get-frame-at",
        control_protocol_parse_request("23 get-frame-at frame=42", &request, &error));
    expect_int("get-frame-at type", CONTROL_COMMAND_GET_FRAME_AT, (int)request.type);
    expect_true("target frame", request.args.frame_ring_target == 42ull);
    expect_true("by frame", !request.args.frame_ring_by_cycle);

    expect_true(
        "frame-ring-record",
        control_protocol_parse_request("24 frame-ring-record off", &request, &error));
    expect_true("record off", !request.args.frame_ring_record_enabled);

    expect_true(
        "history-info",
        control_protocol_parse_request("25 history-info", &request, &error));
    expect_int("history-info type", CONTROL_COMMAND_HISTORY_INFO, (int)request.type);

    expect_true(
        "history-find",
        control_protocol_parse_request(
            "26 history-find address=$C000 access=write limit=16",
            &request,
            &error));
    expect_int("history-find type", CONTROL_COMMAND_HISTORY_FIND, (int)request.type);
    expect_true(
        "history-find text",
        strstr(request.args.history_find_text, "address=$C000") != NULL);

    expect_true(
        "history-read",
        control_protocol_parse_request(
            "27 history-read 42 epoch=1 before=8 after=4", &request, &error));
    expect_true("history-read id", request.args.history_id == 42ull);
    expect_u32("before", 8, request.args.history_before);

    expect_true("line has A2M/6", strstr(line, "A2M/6") != NULL);

    {
        runtime_breakpoint_definition definition;
        char definition_error[128];
        expect_true(
            "composite breakpoint mapping",
            control_parse_breakpoint_definition(
                "write $D000 actions=break ram=aux c100=rom d000=lc2",
                &definition,
                definition_error,
                sizeof(definition_error)));
        expect_int("mapping ram aux", A2SEL48K_AUX, vf_get_ram(definition.mapping));
        expect_int("mapping c100 rom", A2SELC100_ROM, vf_get_c100(definition.mapping));
        expect_int("mapping d000 lc2", A2SELD000_LC_B2, vf_get_d000(definition.mapping));
    }

    control_protocol_format_error(&response, 2, "busy", "deferred", false);
    expect_true(
        "fmt err",
        control_protocol_write_response_line(line, sizeof(line), &response));
    expect_true("err busy", strstr(line, "error busy") != NULL);

    printf("ok\n");
    return 0;
}

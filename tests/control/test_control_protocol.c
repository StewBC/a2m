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

    expect_true("line has A2M/10", strstr(line, "A2M/10") != NULL);

    expect_true(
        "assemble defaults",
        control_protocol_parse_request(
            "80 assemble samples/test.asm", &request, &error));
    expect_int("assemble type", CONTROL_COMMAND_ASSEMBLE, (int)request.type);
    expect_u32("assemble default addr", 0x8000, request.args.address);
    expect_u32("assemble default run", 0x8000, request.args.run_address);
    expect_true("assemble default auto-run off", !request.args.auto_run);
    expect_true("assemble default reset on", request.args.reset_first);
    expect_true("assemble default mli off", !request.args.mli_launch);
    expect_string("assemble path", "samples/test.asm", request.args.path);

    expect_true(
        "assemble options",
        control_protocol_parse_request(
            "81 assemble address=$C000 run-address=$C010 auto-run=1 reset=0 "
            "samples/demo.asm",
            &request,
            &error));
    expect_u32("assemble addr", 0xC000, request.args.address);
    expect_u32("assemble run", 0xC010, request.args.run_address);
    expect_true("assemble auto-run", request.args.auto_run);
    expect_true("assemble reset off", !request.args.reset_first);
    expect_string("assemble path opts", "samples/demo.asm", request.args.path);

    expect_true(
        "assemble mli-launch",
        control_protocol_parse_request(
            "82 assemble mli-launch=1 reset=0 samples/shim.asm",
            &request,
            &error));
    expect_true("mli on", request.args.mli_launch);
    expect_true("mli implies auto-run", request.args.auto_run);
    expect_true("mli forces reset off", !request.args.reset_first);

    expect_true(
        "assemble mli+reset rejected",
        !control_protocol_parse_request(
            "83 assemble mli-launch=1 reset=1 samples/shim.asm",
            &request,
            &error));

    expect_true(
        "find-symbol",
        control_protocol_parse_request("84 find-symbol loop", &request, &error));
    expect_int("find-symbol type", CONTROL_COMMAND_FIND_SYMBOL, (int)request.type);
    expect_string("find-symbol name", "loop", request.args.text);

    expect_true(
        "find-symbol missing name rejected",
        !control_protocol_parse_request("85 find-symbol", &request, &error));

    expect_true(
        "select-disk index",
        control_protocol_parse_request("40 select-disk 3", &request, &error));
    expect_int("select type", CONTROL_COMMAND_SELECT_DISK, (int)request.type);
    expect_u32("select slot resolve", 0, request.args.slot);
    expect_u32("select drive default", 0, request.args.drive);
    expect_u32("select index", 3, request.args.disk_index);

    expect_true(
        "select-disk slot drive index",
        control_protocol_parse_request("41 select-disk 5 1 2", &request, &error));
    expect_u32("select slot", 5, request.args.slot);
    expect_u32("select drive", 1, request.args.drive);
    expect_u32("select index 2", 2, request.args.disk_index);

    expect_true(
        "set-disk-writable",
        control_protocol_parse_request("42 set-disk-writable 0", &request, &error));
    expect_int(
        "writable type", CONTROL_COMMAND_SET_DISK_WRITABLE, (int)request.type);
    expect_u32("writable slot resolve", 0, request.args.slot);
    expect_u32("writable flag", 0, request.args.disk_writable);

    expect_true(
        "set-disk-writable slot drive",
        control_protocol_parse_request("43 set-disk-writable 6 1 1", &request, &error));
    expect_u32("writable slot", 6, request.args.slot);
    expect_u32("writable drive", 1, request.args.drive);
    expect_u32("writable on", 1, request.args.disk_writable);

    expect_true(
        "mount-disk path",
        control_protocol_parse_request("50 mount-disk /tmp/a.nib", &request, &error));
    expect_int("mount type", CONTROL_COMMAND_MOUNT_DISK, (int)request.type);
    expect_u32("mount slot resolve", 0, request.args.slot);
    expect_u32("mount drive 0", 0, request.args.drive);
    expect_true("mount path", strcmp(request.args.path, "/tmp/a.nib") == 0);

    expect_true(
        "mount-disk drive path",
        control_protocol_parse_request("51 mount-disk 1 /tmp/b.nib", &request, &error));
    expect_u32("mount drive 1", 1, request.args.drive);
    expect_u32("mount slot still resolve", 0, request.args.slot);
    expect_true("mount path b", strcmp(request.args.path, "/tmp/b.nib") == 0);

    expect_true(
        "mount-disk slot drive path",
        control_protocol_parse_request(
            "52 mount-disk 5 0 /tmp/c.nib", &request, &error));
    expect_u32("mount explicit slot", 5, request.args.slot);
    expect_u32("mount explicit drive", 0, request.args.drive);
    expect_true("mount path c", strcmp(request.args.path, "/tmp/c.nib") == 0);

    expect_true(
        "mount-disk bad drive rejected",
        !control_protocol_parse_request("53 mount-disk 2 /tmp/d.nib", &request, &error));

    expect_true(
        "mount floppy infer",
        control_protocol_parse_request("60 mount /tmp/a.nib", &request, &error));
    expect_int("mount cmd", CONTROL_COMMAND_MOUNT, (int)request.type);
    expect_u32("mount kind diskii", CONTROL_MEDIA_KIND_DISKII, request.args.media_kind);
    expect_u32("mount resolve slot", 0, request.args.slot);
    expect_true("mount nib path", strcmp(request.args.path, "/tmp/a.nib") == 0);

    expect_true(
        "mount smartport kind",
        control_protocol_parse_request(
            "61 mount kind=smartport /tmp/hd.hdv", &request, &error));
    expect_u32(
        "mount kind sp", CONTROL_MEDIA_KIND_SMARTPORT, request.args.media_kind);
    expect_true("mount hdv path", strcmp(request.args.path, "/tmp/hd.hdv") == 0);

    expect_true(
        "mount smartport slot unit",
        control_protocol_parse_request(
            "62 mount kind=sp 7 1 /tmp/vol.2mg", &request, &error));
    expect_u32("mount sp slot", 7, request.args.slot);
    expect_u32("mount sp unit", 1, request.args.drive);

    expect_true(
        "mount .po without kind rejected",
        !control_protocol_parse_request("63 mount /tmp/x.po", &request, &error));

    expect_true(
        "unmount bare",
        control_protocol_parse_request("70 unmount", &request, &error));
    expect_int("unmount cmd", CONTROL_COMMAND_UNMOUNT, (int)request.type);
    expect_u32("unmount kind unset", CONTROL_MEDIA_KIND_UNSPECIFIED, request.args.media_kind);
    expect_u32("unmount slot resolve", 0, request.args.slot);
    expect_u32("unmount drive 0", 0, request.args.drive);

    expect_true(
        "unmount kind device",
        control_protocol_parse_request("71 unmount kind=diskii 1", &request, &error));
    expect_u32("unmount diskii", CONTROL_MEDIA_KIND_DISKII, request.args.media_kind);
    expect_u32("unmount drive 1", 1, request.args.drive);

    expect_true(
        "unmount slot drive",
        control_protocol_parse_request(
            "72 unmount kind=smartport 7 0", &request, &error));
    expect_u32("unmount sp slot", 7, request.args.slot);
    expect_u32("unmount sp drive", 0, request.args.drive);

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

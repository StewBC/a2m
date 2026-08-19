#include "apple_type_script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

int main(void)
{
    apple_type_event ev[APPLE_TYPE_EVENTS_MAX];
    size_t n = 0;
    apple_type_parse_error err;

    memset(&err, 0, sizeof(err));
    err.offset = -1;
    expect_true(
        "plain",
        apple_type_script_parse("Hi\n", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("plain count", n == 3u);
    expect_true("H", ev[0].kind == APPLE_TYPE_EV_CHAR && ev[0].value == (uint8_t)'H');
    expect_true("i", ev[1].kind == APPLE_TYPE_EV_CHAR && ev[1].value == (uint8_t)'i');
    expect_true("ret", ev[2].kind == APPLE_TYPE_EV_CHAR && ev[2].value == 0x0du);

    n = 0;
    expect_true(
        "j1x",
        apple_type_script_parse("\\[J1X=27]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("j1x count", n == 1u);
    expect_true("j1x kind", ev[0].kind == APPLE_TYPE_EV_AXIS_SET);
    expect_true("j1x stick", ev[0].stick == 0u && ev[0].axis_or_btn == 0u);
    expect_true("j1x val", ev[0].value == 27u);

    n = 0;
    expect_true(
        "j1c",
        apple_type_script_parse("\\[J1C]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("j1c two axes", n == 2u);
    expect_true("j1c x", ev[0].value == 128u && ev[0].axis_or_btn == 0u);
    expect_true("j1c y", ev[1].value == 128u && ev[1].axis_or_btn == 1u);

    n = 0;
    expect_true(
        "j1xl",
        apple_type_script_parse("\\[J1XL]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("j1xl", n == 1u && ev[0].value == 0u && ev[0].axis_or_btn == 0u);

    n = 0;
    expect_true(
        "oa pulse",
        apple_type_script_parse("\\[OA]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("oa pulse 3", n == 3u);
    expect_true("oa+", ev[0].kind == APPLE_TYPE_EV_OA_SET && ev[0].value == 1u);
    expect_true("oa wait", ev[1].kind == APPLE_TYPE_EV_WAIT && ev[1].wait_count == 1u);
    expect_true("oa-", ev[2].kind == APPLE_TYPE_EV_OA_SET && ev[2].value == 0u);

    n = 0;
    expect_true(
        "reset",
        apple_type_script_parse("\\[RESET]\\[COLDRESET]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("reset kinds", n == 2u &&
        ev[0].kind == APPLE_TYPE_EV_RESET_WARM &&
        ev[1].kind == APPLE_TYPE_EV_RESET_COLD);

    n = 0;
    expect_true(
        "mixed",
        apple_type_script_parse(
            "\\[J1X=0]RUN\\[W:2]\\[J1C]",
            ev,
            APPLE_TYPE_EVENTS_MAX,
            &n,
            &err));
    expect_true("mixed has run", n >= 6u);

    n = 0;
    expect_true(
        "escaped cr",
        apple_type_script_parse("1\\rO", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("escaped cr count", n == 3u);
    expect_true("escaped cr 1", ev[0].kind == APPLE_TYPE_EV_CHAR && ev[0].value == (uint8_t)'1');
    expect_true("escaped cr ret", ev[1].kind == APPLE_TYPE_EV_CHAR && ev[1].value == 0x0du);
    expect_true("escaped cr O", ev[2].kind == APPLE_TYPE_EV_CHAR && ev[2].value == (uint8_t)'O');

    n = 0;
    err.offset = -1;
    expect_true(
        "bad token fails",
        !apple_type_script_parse("\\[NOPE]", ev, APPLE_TYPE_EVENTS_MAX, &n, &err));
    expect_true("err set", err.offset >= 0 && err.message != NULL);

    printf("ok\n");
    return 0;
}

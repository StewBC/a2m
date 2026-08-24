/* Normative history-find option grammar (runtime_history_query_parse). */
#include "runtime_history.h"
#include "runtime_history_query_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static bool parse(
    const char *text,
    runtime_history_query *query,
    runtime_history_from_kind *from_kind,
    uint64_t *from_id,
    uint16_t *limit)
{
    return runtime_history_parse_find_options(
        text, query, from_kind, from_id, limit);
}

static void expect_parse_ok(const char *name, const char *text)
{
    runtime_history_query query;
    runtime_history_from_kind from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
    uint64_t from_id = 0u;
    uint16_t limit = 0u;
    expect_true(name, parse(text, &query, &from_kind, &from_id, &limit));
}

static void expect_parse_fail(const char *name, const char *text)
{
    runtime_history_query query;
    runtime_history_from_kind from_kind = RUNTIME_HISTORY_FROM_DEFAULT;
    uint64_t from_id = 0u;
    uint16_t limit = 0u;
    expect_true(name, !parse(text, &query, &from_kind, &from_id, &limit));
}

static int key_in_table(const char *key, const char *const *table)
{
    size_t i;
    for (i = 0u; table[i] != NULL; ++i) {
        if (strcmp(table[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    runtime_history_query query;
    runtime_history_from_kind from_kind;
    uint64_t from_id;
    uint16_t limit;
    const char *const *keys;
    const char *const *access_names;
    size_t i;

    /* Defaults on empty / NULL. */
    expect_true(
        "empty",
        parse("", &query, &from_kind, &from_id, &limit));
    expect_true("empty dir", query.direction == RUNTIME_HISTORY_QUERY_BACKWARD);
    expect_true("empty limit", limit == 64u);
    expect_true("empty from", from_kind == RUNTIME_HISTORY_FROM_DEFAULT);
    expect_true(
        "null text",
        parse(NULL, &query, &from_kind, &from_id, &limit));

    /* Live six-key baseline. */
    expect_true(
        "six-key",
        parse(
            "pc=$FC00-$FCFF address=$C000 access=data-write direction=forward "
            "limit=16 from=newest",
            &query,
            &from_kind,
            &from_id,
            &limit));
    expect_true("pc", query.has_pc && query.pc_first == 0xfc00u &&
                          query.pc_last == 0xfcffu);
    expect_true(
        "address",
        query.has_address && query.address_first == 0xc000u &&
            query.address_last == 0xc000u);
    expect_true("access write", query.has_access &&
                                    (query.access_mask &
                                     RUNTIME_HISTORY_ACCESS_DATA_WRITE) != 0u);
    expect_true("dir fwd", query.direction == RUNTIME_HISTORY_QUERY_FORWARD);
    expect_true("limit 16", limit == 16u);
    expect_true("from newest", from_kind == RUNTIME_HISTORY_FROM_NEWEST);

    /* Expanded keys. */
    expect_true(
        "epoch timeline cycle",
        parse(
            "epoch=3 timeline=1 cycle=1000-2000",
            &query,
            &from_kind,
            &from_id,
            &limit));
    expect_true("epoch", query.has_epoch && query.epoch == 3u);
    expect_true("timeline", query.has_timeline && query.timeline == 1u);
    expect_true(
        "cycle range",
        query.has_cycle && query.cycle_first == 1000u &&
            query.cycle_last == 2000u);

    expect_true(
        "cycle single 0x",
        parse("cycle=0x10", &query, &from_kind, &from_id, &limit));
    expect_true(
        "cycle single",
        query.has_cycle && query.cycle_first == 0x10u &&
            query.cycle_last == 0x10u);

    expect_true(
        "value decimal",
        parse("value=34", &query, &from_kind, &from_id, &limit));
    expect_true(
        "value dec bits",
        query.has_value && query.value == 34u && query.value_mask == 0xffu);

    expect_true(
        "value hex",
        parse("value=$22", &query, &from_kind, &from_id, &limit));
    expect_true(
        "value hex bits",
        query.has_value && query.value == 0x22u && query.value_mask == 0xffu);

    expect_true(
        "value nibble",
        parse("value=$2?", &query, &from_kind, &from_id, &limit));
    expect_true(
        "value nibble bits",
        query.has_value && query.value == 0x20u && query.value_mask == 0xf0u);

    expect_true(
        "value any",
        parse("value=$??", &query, &from_kind, &from_id, &limit));
    expect_true(
        "value any bits",
        query.has_value && query.value == 0u && query.value_mask == 0u);

    expect_true(
        "value 0x nibble",
        parse("value=0x2?", &query, &from_kind, &from_id, &limit));
    expect_true(
        "value 0x nibble bits",
        query.has_value && query.value == 0x20u && query.value_mask == 0xf0u);

    expect_true(
        "opcodes",
        parse("opcodes=A9,??,8D", &query, &from_kind, &from_id, &limit));
    expect_true("opcodes len", query.opcode_pattern_length == 3u);
    expect_true(
        "opcodes[0]",
        query.opcode_pattern[0].value == 0xa9u &&
            query.opcode_pattern[0].mask == 0xffu);
    expect_true(
        "opcodes[1]",
        query.opcode_pattern[1].value == 0u &&
            query.opcode_pattern[1].mask == 0u);
    expect_true(
        "opcodes[2]",
        query.opcode_pattern[2].value == 0x8du &&
            query.opcode_pattern[2].mask == 0xffu);

    expect_true(
        "opcodes nibble",
        parse("opcodes=8?,?D", &query, &from_kind, &from_id, &limit));
    expect_true("opcodes nibble len", query.opcode_pattern_length == 2u);
    expect_true(
        "opcodes 8?",
        query.opcode_pattern[0].value == 0x80u &&
            query.opcode_pattern[0].mask == 0xf0u);
    expect_true(
        "opcodes ?D",
        query.opcode_pattern[1].value == 0x0du &&
            query.opcode_pattern[1].mask == 0x0fu);

    /* Access specials and fine names. */
    expect_true(
        "access execute",
        parse("access=execute", &query, &from_kind, &from_id, &limit));
    expect_true("execute no access", !query.has_access);

    expect_true(
        "access fetch",
        parse("access=fetch", &query, &from_kind, &from_id, &limit));
    expect_true("fetch no access", !query.has_access);

    expect_true(
        "access operand",
        parse("access=operand", &query, &from_kind, &from_id, &limit));
    expect_true(
        "operand mask",
        query.has_access &&
            query.access_mask == RUNTIME_HISTORY_ACCESS_OPERAND);

    expect_true(
        "access stack-write",
        parse("access=stack-write", &query, &from_kind, &from_id, &limit));
    expect_true(
        "stack-write mask",
        query.has_access &&
            query.access_mask == RUNTIME_HISTORY_ACCESS_STACK_WRITE);

    expect_true(
        "access vector-read",
        parse("access=vector-read", &query, &from_kind, &from_id, &limit));
    expect_true(
        "vector-read mask",
        query.has_access &&
            query.access_mask == RUNTIME_HISTORY_ACCESS_VECTOR_READ);

    expect_true(
        "access dummy-read",
        parse("access=dummy-read", &query, &from_kind, &from_id, &limit));
    expect_true(
        "dummy-read mask",
        query.has_access &&
            query.access_mask == RUNTIME_HISTORY_ACCESS_DUMMY_READ);

    expect_true(
        "access rmw-dummy-write",
        parse("access=rmw-dummy-write", &query, &from_kind, &from_id, &limit));
    expect_true(
        "rmw mask",
        query.has_access &&
            query.access_mask == RUNTIME_HISTORY_ACCESS_RMW_DUMMY_WRITE);

    expect_true(
        "from id",
        parse("from=42", &query, &from_kind, &from_id, &limit));
    expect_true(
        "from id bits",
        from_kind == RUNTIME_HISTORY_FROM_ID && from_id == 42u);

    expect_true(
        "from oldest",
        parse("from=oldest", &query, &from_kind, &from_id, &limit));
    expect_true("from oldest kind", from_kind == RUNTIME_HISTORY_FROM_OLDEST);

    /* Last-wins on duplicate keys. */
    expect_true(
        "dup last-wins",
        parse(
            "limit=8 limit=32 pc=$1000 pc=$2000",
            &query,
            &from_kind,
            &from_id,
            &limit));
    expect_true("dup limit", limit == 32u);
    expect_true(
        "dup pc",
        query.has_pc && query.pc_first == 0x2000u && query.pc_last == 0x2000u);

    /* Failures. */
    expect_parse_fail("unknown key", "bogus=1");
    expect_parse_fail("no equals", "pc");
    expect_parse_fail("empty value", "pc=");
    expect_parse_fail("bad access", "access=fly");
    expect_parse_fail("bad direction", "direction=sideways");
    expect_parse_fail("limit 0", "limit=0");
    expect_parse_fail("limit 257", "limit=257");
    expect_parse_fail("from 0", "from=0");
    expect_parse_fail("cycle dollar", "cycle=$100");
    expect_parse_fail("value decimal wildcard", "value=2?");
    expect_parse_fail("opcodes dollar", "opcodes=$A9");
    expect_parse_fail("opcodes empty", "opcodes=");
    expect_parse_fail("pc inverted", "pc=$2000-$1000");
    expect_parse_fail("cycle inverted", "cycle=200-100");

    /* Parsed queries must pass history_query_is_valid via find INVALID path:
     * exercise fields the matcher already honors. */
    expect_true(
        "combo valid shape",
        parse(
            "address=$2011 access=data-write value=$22 "
            "opcodes=A9,??,8D cycle=1-999999 direction=backward limit=64",
            &query,
            &from_kind,
            &from_id,
            &limit));
    expect_true("combo address", query.has_address);
    expect_true("combo value", query.has_value && query.value == 0x22u);
    expect_true("combo opcodes", query.opcode_pattern_length == 3u);
    expect_true("combo cycle", query.has_cycle);

    /* Public key / access tables are the autocomplete source of truth. */
    keys = runtime_history_find_option_keys();
    access_names = runtime_history_find_access_names();
    expect_true("keys non-null", keys != NULL && keys[0] != NULL);
    expect_true("access non-null", access_names != NULL && access_names[0] != NULL);
    expect_true("has pc", key_in_table("pc", keys));
    expect_true("has opcodes", key_in_table("opcodes", keys));
    expect_true("has epoch", key_in_table("epoch", keys));
    expect_true("has cycle", key_in_table("cycle", keys));
    expect_true("has value", key_in_table("value", keys));
    expect_true("has timeline", key_in_table("timeline", keys));
    expect_true("has fetch", key_in_table("fetch", access_names));
    expect_true("has operand", key_in_table("operand", access_names));
    expect_true("has data-write", key_in_table("data-write", access_names));
    expect_true(
        "has rmw-dummy-write", key_in_table("rmw-dummy-write", access_names));

    for (i = 0u; keys[i] != NULL; ++i) {
        /* Every published key must parse with a trivial valid value. */
        char sample[64];
        if (strcmp(keys[i], "opcodes") == 0) {
            snprintf(sample, sizeof(sample), "%s=A9", keys[i]);
        } else if (strcmp(keys[i], "access") == 0) {
            snprintf(sample, sizeof(sample), "%s=data-read", keys[i]);
        } else if (strcmp(keys[i], "direction") == 0) {
            snprintf(sample, sizeof(sample), "%s=backward", keys[i]);
        } else if (strcmp(keys[i], "from") == 0) {
            snprintf(sample, sizeof(sample), "%s=newest", keys[i]);
        } else if (strcmp(keys[i], "limit") == 0) {
            snprintf(sample, sizeof(sample), "%s=8", keys[i]);
        } else if (
            strcmp(keys[i], "pc") == 0 || strcmp(keys[i], "address") == 0) {
            snprintf(sample, sizeof(sample), "%s=$1000", keys[i]);
        } else if (strcmp(keys[i], "value") == 0) {
            snprintf(sample, sizeof(sample), "%s=$00", keys[i]);
        } else {
            snprintf(sample, sizeof(sample), "%s=1", keys[i]);
        }
        expect_parse_ok(keys[i], sample);
    }

    for (i = 0u; access_names[i] != NULL; ++i) {
        char sample[64];
        snprintf(sample, sizeof(sample), "access=%s", access_names[i]);
        expect_parse_ok(access_names[i], sample);
    }

    printf("ok\n");
    return 0;
}

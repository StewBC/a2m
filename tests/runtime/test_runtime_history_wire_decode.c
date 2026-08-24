/* HST1 wire decode: round-trip vs encode + Python Ctl.decode_hst1 golden. */
#include "runtime_history.h"
#include "runtime_history_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef A2M_SOURCE_DIR
#define A2M_SOURCE_DIR "."
#endif

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void fill_instruction(runtime_history_record *r, uint64_t epoch)
{
    memset(r, 0, sizeof(*r));
    r->epoch = epoch;
    r->id = 13523u;
    r->timeline = 1u;
    r->machine_cycle = 1234u;
    r->kind = RUNTIME_HISTORY_RECORD_INSTRUCTION;
    r->pc = 0xfcacu;
    r->a = 0x00u;
    r->x = 0x00u;
    r->y = 0x00u;
    r->sp = 0xf2u;
    r->p = 0x24u;
    r->opcode = 0xd0u;
    r->operand1 = 0x05u;
    r->operand2 = 0x00u;
    r->instruction_length = 2u;
    r->access_count = 2u;
    r->accesses[0].address = 0xc000u;
    r->accesses[0].cycle_offset = 1u;
    r->accesses[0].value = 0x22u;
    r->accesses[0].kind = C6510_BUS_ACCESS_DATA_WRITE;
    r->accesses[1].address = 0xfcadu;
    r->accesses[1].cycle_offset = 0u;
    r->accesses[1].value = 0x05u;
    r->accesses[1].kind = C6510_BUS_ACCESS_OPERAND_READ;
    r->partial = false;
    r->access_truncated = true;
    r->timing_truncated = false;
}

static void fill_marker(runtime_history_record *r, uint64_t epoch)
{
    memset(r, 0, sizeof(*r));
    r->epoch = epoch;
    r->id = 13524u;
    r->timeline = 1u;
    r->machine_cycle = 1200u;
    r->kind = RUNTIME_HISTORY_RECORD_MARKER;
    r->marker_kind = RUNTIME_HISTORY_MARKER_MEDIA_CHANGED;
    r->marker_arg0 = 1u;
    r->marker_arg1 = (6u << 8) | 0u;
    r->access_count = 0u;
}

static int records_equal(
    const runtime_history_record *a,
    const runtime_history_record *b)
{
    size_t i;
    if (a->epoch != b->epoch || a->id != b->id || a->timeline != b->timeline ||
        a->machine_cycle != b->machine_cycle || a->kind != b->kind ||
        a->pc != b->pc || a->a != b->a || a->x != b->x || a->y != b->y ||
        a->sp != b->sp || a->p != b->p || a->opcode != b->opcode ||
        a->operand1 != b->operand1 || a->operand2 != b->operand2 ||
        a->instruction_length != b->instruction_length ||
        a->access_count != b->access_count ||
        a->marker_kind != b->marker_kind ||
        a->marker_arg0 != b->marker_arg0 ||
        a->marker_arg1 != b->marker_arg1 || a->partial != b->partial ||
        a->access_truncated != b->access_truncated ||
        a->timing_truncated != b->timing_truncated) {
        return 0;
    }
    for (i = 0u; i < a->access_count; ++i) {
        if (a->accesses[i].address != b->accesses[i].address ||
            a->accesses[i].cycle_offset != b->accesses[i].cycle_offset ||
            a->accesses[i].value != b->accesses[i].value ||
            a->accesses[i].kind != b->accesses[i].kind) {
            return 0;
        }
    }
    return 1;
}

static void test_round_trip(void)
{
    const uint64_t epoch = 7u;
    runtime_history_record src[2];
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    size_t encoded = 0u;
    bool clipped = false;
    uint64_t decoded_epoch = 0u;
    runtime_history_record *decoded = NULL;
    bool *anchors = NULL;
    size_t count = 0u;

    fill_instruction(&src[0], epoch);
    fill_marker(&src[1], epoch);

    expect_true(
        "encode",
        runtime_history_wire_encode(
            epoch, src, 2u, false, src[0].id, &bytes, &length, &encoded,
            &clipped) == RUNTIME_HISTORY_WIRE_OK);
    expect_true("encode count", encoded == 2u && !clipped && bytes != NULL);
    expect_true("magic", length >= 4u && memcmp(bytes, "HST1", 4) == 0);

    expect_true(
        "decode",
        runtime_history_wire_decode(
            bytes, length, &decoded_epoch, &decoded, &anchors, &count) ==
            RUNTIME_HISTORY_WIRE_OK);
    expect_true("epoch", decoded_epoch == epoch);
    expect_true("count", count == 2u && decoded != NULL && anchors != NULL);
    expect_true("rec0", records_equal(&src[0], &decoded[0]));
    expect_true("rec1", records_equal(&src[1], &decoded[1]));
    expect_true("anchor0", anchors[0]);
    expect_true("anchor1", !anchors[1]);

    free(bytes);
    free(decoded);
    free(anchors);
}

static void test_empty_page(void)
{
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    size_t encoded = 0u;
    bool clipped = false;
    uint64_t epoch = 0u;
    runtime_history_record *decoded = NULL;
    size_t count = 99u;

    expect_true(
        "encode empty",
        runtime_history_wire_encode(
            3u, NULL, 0u, true, 0u, &bytes, &length, &encoded, &clipped) ==
            RUNTIME_HISTORY_WIRE_OK);
    expect_true(
        "empty size",
        encoded == 0u && length == RUNTIME_HISTORY_WIRE_HEADER_SIZE);
    expect_true(
        "decode empty",
        runtime_history_wire_decode(
            bytes, length, &epoch, &decoded, NULL, &count) ==
            RUNTIME_HISTORY_WIRE_OK);
    expect_true("empty outs", epoch == 3u && count == 0u && decoded == NULL);
    free(bytes);
}

static void test_invalid(void)
{
    uint8_t bad[32];
    uint64_t epoch = 0u;
    runtime_history_record *decoded = NULL;
    size_t count = 0u;

    memset(bad, 0, sizeof(bad));
    expect_true(
        "short",
        runtime_history_wire_decode(
            bad, 8u, &epoch, &decoded, NULL, &count) ==
            RUNTIME_HISTORY_WIRE_INVALID);

    memcpy(bad, "HST1", 4);
    bad[4] = 2; /* version 2 */
    expect_true(
        "bad version",
        runtime_history_wire_decode(
            bad, RUNTIME_HISTORY_WIRE_HEADER_SIZE, &epoch, &decoded, NULL,
            &count) == RUNTIME_HISTORY_WIRE_INVALID);

    bad[4] = 1;
    bad[6] = 1; /* nonzero flags */
    expect_true(
        "bad flags",
        runtime_history_wire_decode(
            bad, RUNTIME_HISTORY_WIRE_HEADER_SIZE, &epoch, &decoded, NULL,
            &count) == RUNTIME_HISTORY_WIRE_INVALID);
}

static void test_python_golden(void)
{
    const uint64_t epoch = 7u;
    runtime_history_record src[2];
    uint8_t *bytes = NULL;
    uint32_t length = 0u;
    size_t encoded = 0u;
    bool clipped = false;
    char path[] = "/tmp/a2m_hst1_goldenXXXXXX";
    int fd;
    FILE *fp;
    char cmd[512];
    int rc;

    fill_instruction(&src[0], epoch);
    fill_marker(&src[1], epoch);
    expect_true(
        "golden encode",
        runtime_history_wire_encode(
            epoch, src, 2u, false, src[0].id, &bytes, &length, &encoded,
            &clipped) == RUNTIME_HISTORY_WIRE_OK);

    fd = mkstemp(path);
    expect_true("mkstemp", fd >= 0);
    fp = fdopen(fd, "wb");
    expect_true("fdopen", fp != NULL);
    expect_true("fwrite", fwrite(bytes, 1, length, fp) == length);
    expect_true("fclose", fclose(fp) == 0);
    free(bytes);

    /*
     * Cross-check against tools/a2m_control_client.py Ctl.decode_hst1 —
     * same validation the Forensics path will rely on.
     */
    snprintf(
        cmd,
        sizeof(cmd),
        "python3 \"%s/tests/runtime/check_hst1_decode_golden.py\" \"%s\"",
        A2M_SOURCE_DIR,
        path);
    rc = system(cmd);
    remove(path);
    expect_true("python decode_hst1", rc == 0);
}

int main(void)
{
    test_round_trip();
    test_empty_page();
    test_invalid();
    test_python_golden();
    printf("ok\n");
    return 0;
}

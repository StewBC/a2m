#include "apple2.h"
#include "apple2_snapshot.h"

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
        fprintf(stderr, "FAIL: %s: expected true\n", name);
        exit(1);
    }
}

int main(void)
{
    apple2_t m;
    apple2_t m2;
    uint8_t *buf = NULL;
    size_t size;
    size_t written;
    uint8_t marker[4] = { 0xCA, 0xFE, 0xBA, 0xBE };
    uint16_t pc_saved;
    uint64_t cycles_saved;
    uint32_t flags_saved;
    uint16_t line_saved;
    uint16_t h_saved;
    uint32_t prng_saved;

    expect_true("init", apple2_init(&m));
    apple2_debug_write(&m, 0x0400, marker[0]);
    apple2_debug_write(&m, 0x0401, marker[1]);
    apple2_debug_write(&m, 0x0402, marker[2]);
    apple2_debug_write(&m, 0x0403, marker[3]);
    (void)apple2_step_instruction(&m);
    (void)apple2_step_instruction(&m);
    (void)apple2_step_instruction(&m);

    pc_saved = m.cpu.cpu.pc;
    cycles_saved = m.cpu.cpu.cycles;
    flags_saved = m.state_flags;
    line_saved = m.video.line;
    h_saved = m.video.cycle_in_line;
    (void)apple2_rand_u32(&m);
    prng_saved = m.prng;

    expect_true("flush", apple2_snapshot_flush_media(&m));
    size = apple2_snapshot_size(&m);
    expect_true("size>0", size > 0);
    buf = (uint8_t *)malloc(size);
    expect_true("malloc", buf != NULL);
    written = apple2_snapshot_save(&m, buf, size);
    expect_true("save", written == size);

    /* Mutate then load into a second machine. */
    expect_true("init2", apple2_init(&m2));
    m2.cpu.cpu.pc = 0x1234;
    m2.cpu.cpu.cycles = 99999;
    apple2_debug_write(&m2, 0x0400, 0x00);

    expect_true("load", apple2_snapshot_load(&m2, buf, written));
    if (m2.cpu.cpu.pc != pc_saved) {
        fprintf(stderr, "FAIL: pc %04x != %04x\n", m2.cpu.cpu.pc, pc_saved);
        exit(1);
    }
    if (m2.cpu.cpu.cycles != cycles_saved) {
        fprintf(
            stderr,
            "FAIL: cycles %llu != %llu\n",
            (unsigned long long)m2.cpu.cpu.cycles,
            (unsigned long long)cycles_saved);
        exit(1);
    }
    if (m2.state_flags != flags_saved) {
        fprintf(stderr, "FAIL: flags %08x != %08x\n", m2.state_flags, flags_saved);
        exit(1);
    }
    if (m2.video.line != line_saved || m2.video.cycle_in_line != h_saved) {
        fail("beam not restored");
    }
    if (m2.prng != prng_saved) {
        fprintf(stderr, "FAIL: prng %08x != %08x\n", m2.prng, prng_saved);
        exit(1);
    }
    if (apple2_debug_read(&m2, 0x0400) != marker[0] ||
        apple2_debug_read(&m2, 0x0401) != marker[1] ||
        apple2_debug_read(&m2, 0x0402) != marker[2] ||
        apple2_debug_read(&m2, 0x0403) != marker[3]) {
        fail("RAM marker");
    }

    /* Bad magic must fail. */
    buf[0] ^= 0xFFu;
    expect_true("bad magic fails", !apple2_snapshot_load(&m2, buf, written));

    free(buf);
    apple2_shutdown(&m);
    apple2_shutdown(&m2);
    printf("ok\n");
    return 0;
}

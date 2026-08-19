#include "apple2.h"

#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

int main(void)
{
    apple2_t machine;

    if (!apple2_init(&machine)) {
        fail("init");
    }
    if (!machine.ready) {
        fail("not ready");
    }
    if (machine.ram_main == NULL || machine.ram_lc == NULL) {
        fail("no ram");
    }

    apple2_debug_write(&machine, 0x1234, 0xab);
    if (apple2_debug_read(&machine, 0x1234) != 0xab) {
        fail("debug r/w");
    }

    apple2_load(&machine, 0x8000, (const uint8_t *)"\x01\x02\x03", 3);
    if (apple2_debug_read(&machine, 0x8001) != 0x02) {
        fail("load");
    }

    apple2_set_model(&machine, APPLE2_MODEL_II_PLUS);
    if (machine.cpu.cpu.class != CPU_6502) {
        fail("ii+ class");
    }
    apple2_set_model(&machine, APPLE2_MODEL_IIE_ENHANCED);
    if (machine.cpu.cpu.class != CPU_65c02) {
        fail("iie class");
    }

    apple2_reset(&machine);
    apple2_shutdown(&machine);

    if (apple2_init(NULL)) {
        fail("NULL init should fail");
    }

    printf("apple2_stub: all tests passed\n");
    return 0;
}

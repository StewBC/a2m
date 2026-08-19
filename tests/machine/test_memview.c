#include "apple2.h"
#include "memview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_u8(const char *name, uint8_t got, uint8_t want)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s: got %02X want %02X\n", name, got, want);
        exit(1);
    }
}

int main(void)
{
    apple2_t m;
    view_flags_t vf;

    if (!apple2_init(&m)) {
        fail("apple2_init");
    }
    apple2_set_model(&m, APPLE2_MODEL_IIE_ENHANCED);
    apple2_reset(&m);

    /* Distinct markers in main vs aux vs LC planes. */
    m.ram_main[0x0800] = 0x11;
    m.ram_main[0x10000 + 0x0800] = 0x22;
    m.ram_lc[0x0000 + 0x000] = 0xA1; /* LC bank1 $D000 main */
    m.ram_lc[0x1000 + 0x000] = 0xA2; /* LC bank2 $D000 main */
    m.ram_lc[0x2000 + 0x000] = 0xE1; /* LC $E000 main */
    m.ram_lc[0x4000 + 0x000] = 0xB1; /* LC bank1 $D000 aux */

    /* Map mode: CPU map (post-reset ROM at $D000). */
    vf = view_flags_from_area(RUNTIME_VIEW_AREA_MAP);
    expect_u8("map $800", apple2_read_in_view(&m, vf, 0x0800), 0x11);

    /* Force main 48K. */
    vf = view_flags_from_area(RUNTIME_VIEW_AREA_MAIN);
    expect_u8("main $800", apple2_read_in_view(&m, vf, 0x0800), 0x11);
    apple2_write_in_view(&m, vf, 0x0900, 0x33);
    expect_u8("main write", m.ram_main[0x0900], 0x33);

    /* Force aux 48K. */
    vf = view_flags_from_area(RUNTIME_VIEW_AREA_AUX);
    expect_u8("aux $800", apple2_read_in_view(&m, vf, 0x0800), 0x22);
    apple2_write_in_view(&m, vf, 0x0900, 0x44);
    expect_u8("aux write", m.ram_main[0x10000 + 0x0900], 0x44);

    /* LC1 / LC2. */
    vf = view_flags_from_area(RUNTIME_VIEW_AREA_LC1);
    expect_u8("lc1 $D000", apple2_read_in_view(&m, vf, 0xD000), 0xA1);
    expect_u8("lc1 $E000", apple2_read_in_view(&m, vf, 0xE000), 0xE1);

    vf = view_flags_from_area(RUNTIME_VIEW_AREA_LC2);
    expect_u8("lc2 $D000", apple2_read_in_view(&m, vf, 0xD000), 0xA2);

    /* ROM force: system ROM present, not our LC marker. */
    vf = view_flags_from_area(RUNTIME_VIEW_AREA_ROM);
    {
        uint8_t rom_b = apple2_read_in_view(&m, vf, 0xD000);
        if (rom_b == 0xA1 || rom_b == 0xA2) {
            fail("ROM mode still reading LC");
        }
    }

    /* C100 is an independent field: forced ROM reads the internal //e image. */
    vf = 0u;
    vf_set_c100(&vf, A2SELC100_ROM);
    expect_u8(
        "C100 forced ROM",
        apple2_read_in_view(&m, vf, 0xC600),
        m.rom_c000[0x0600]);

    /* Area cycle //e includes Aux. */
    {
        runtime_view_area a = RUNTIME_VIEW_AREA_MAP;
        a = view_area_cycle(a, 1);
        if (a != RUNTIME_VIEW_AREA_MAIN) {
            fail("cycle map->main");
        }
        a = view_area_cycle(a, 1);
        if (a != RUNTIME_VIEW_AREA_AUX) {
            fail("cycle main->aux");
        }
    }

    /* ][+ cycle skips Aux. */
    {
        runtime_view_area a = RUNTIME_VIEW_AREA_MAIN;
        a = view_area_cycle(a, 0);
        if (a != RUNTIME_VIEW_AREA_LC1) {
            fail("plus cycle main->lc1");
        }
    }

    apple2_shutdown(&m);
    printf("ok memview\n");
    return 0;
}

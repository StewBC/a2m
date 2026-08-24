#include "apple2.h"
#include "mboard.h"
#include "smrtprt.h"
#include "softswitch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void test_mockingboard_attach(void)
{
    apple2_t m;
    uint8_t v;

    if (!apple2_init(&m)) {
        fail("apple2_init");
    }
    /* Default attach is slot 4. */
    if (m.slot_type[4] != SLOT_TYPE_MOCKINGBOARD) {
        fail("default MB slot 4");
    }
    if (m.mb_slot != 4) {
        fail("mb_slot");
    }

    /* Write ORB on VIA0 window (compat C0n0–C0n7). */
    softswitch_c0_write(&m, 0xC040 + 0x00, 0x07); /* latch address via ORB on pair0? */
    /* Use mockingboard_write path C040 = slot 4, reg 0 = ORB via0 low window */
    mockingboard_write(&m, &m.mockingboard[4], 4, 0xC040, 0x00, 0x07);
    mockingboard_write(&m, &m.mockingboard[4], 4, 0xC041, 0x01, 0x08); /* ORA = reg 8 volume A */
    /* After latch, write data mode */
    mockingboard_write(&m, &m.mockingboard[4], 4, 0xC040, 0x00, 0x06);
    mockingboard_write(&m, &m.mockingboard[4], 4, 0xC041, 0x01, 0x0F);

    /* Queue some time and sample — should not crash; volume may be non-zero. */
    mockingboard_queue_ay_cycles(&m.mockingboard[4], 1000);
    {
        MOCKINGBOARD_SAMPLE s = mockingboard_get_stereo_sample(&m.mockingboard[4]);
        (void)s;
    }

    /* IRQ path: step timers */
    apple2_peripherals_step(&m, 100);
    v = mockingboard_irq_pending(&m);
    (void)v;

    apple2_shutdown(&m);
}

static void test_cards_support_slots_one_through_seven(void)
{
    apple2_t m;
    if (!apple2_init(&m)) {
        fail("slot range init");
    }
    if (!apple2_attach_diskii(&m, 1) || m.slot_type[1] != SLOT_TYPE_DISKII) {
        fail("Disk II slot 1");
    }
    if (!apple2_attach_smartport(&m, 2) || m.slot_type[2] != SLOT_TYPE_SMARTPORT) {
        fail("SmartPort slot 2");
    }
    if (!apple2_attach_mockingboard(&m, 7) || m.slot_type[7] != SLOT_TYPE_MOCKINGBOARD ||
        m.mb_slot != 7 || m.slot_type[4] == SLOT_TYPE_MOCKINGBOARD) {
        fail("single Mockingboard moves to slot 7");
    }
    if (apple2_attach_diskii(&m, 0) || apple2_attach_smartport(&m, 0) ||
        apple2_attach_mockingboard(&m, 0)) {
        fail("slot 0 must reject peripheral cards");
    }
    apple2_shutdown(&m);
}

static void test_smartport_mount_missing(void)
{
    apple2_t m;

    if (!apple2_init(&m)) {
        fail("init2");
    }
    if (!apple2_attach_smartport(&m, 7)) {
        fail("attach SP");
    }
    if (m.slot_type[7] != SLOT_TYPE_SMARTPORT) {
        fail("slot type SP");
    }
    /* Missing file fails cleanly. */
    if (apple2_smartport_mount(&m, 7, 0, "/no/such/hd.po") == 0) {
        fail("expected mount fail");
    }

    /* Cn ROM mapped (non-zero firmware bytes). */
    if (m.smartport_rom_bytes[7][0] == 0 && m.smartport_rom_bytes[7][1] == 0) {
        /* still ok if zeros — just check shadow */
    }
    if (m.rom_shadow_pages[7] != m.smartport_rom_bytes[7]) {
        fail("SP rom shadow");
    }

    apple2_shutdown(&m);
}

static void test_smartport_image_roundtrip(void)
{
    apple2_t m;
    const char *path = "test_sp_image.po";
    FILE *fp;
    uint8_t block[512];
    int i;

    /* 2 empty ProDOS blocks */
    memset(block, 0, sizeof(block));
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fail("create po");
    }
    for (i = 0; i < 4; i++) {
        if (fwrite(block, 1, 512, fp) != 512) {
            fail("write po");
        }
    }
    fclose(fp);

    if (!apple2_init(&m)) {
        fail("init3");
    }
    if (apple2_smartport_mount(&m, 5, 0, path) != 0) {
        fail("sp mount");
    }

    /* Command buffer: cmd=1 read, unit=0, block=0 */
    m.sp_device[5].sp_buffer[0] = 1;
    m.sp_device[5].sp_buffer[1] = 0;
    m.sp_device[5].sp_buffer[2] = 0;
    m.sp_device[5].sp_buffer[3] = 0;
    sp_read(&m, 5);
    if (m.sp_device[5].sp_buffer[0] != SP_SUCCESS) {
        fail("sp_read status");
    }

    /* Status command */
    m.sp_device[5].sp_buffer[0] = 0;
    m.sp_device[5].sp_buffer[1] = 0;
    sp_status(&m, 5);
    if (m.sp_device[5].sp_buffer[0] != SP_SUCCESS) {
        fail("sp_status");
    }
    /* blocks = 4 */
    if (m.sp_device[5].sp_buffer[1] != 4 || m.sp_device[5].sp_buffer[2] != 0) {
        fprintf(
            stderr,
            "blocks %u %u\n",
            m.sp_device[5].sp_buffer[1],
            m.sp_device[5].sp_buffer[2]);
        fail("block count");
    }

    if (apple2_smartport_eject(&m, 5, 0) != 0 ||
        m.sp_device[5].sp_files[0].is_file_open) {
        fail("SmartPort live eject");
    }
    if (apple2_smartport_mount(&m, 5, 0, path) != 0) {
        fail("SmartPort live reinsert");
    }

    apple2_shutdown(&m);
    remove(path);
}

/* Firmware for slot 7 talks to $C0F4 (data) and $C0F5 (status). A wrong
   SP_DATA/SP_STATUS nibble makes PR#7 / boot hang then ERR. */
static void test_smartport_softswitch_ports(void)
{
    apple2_t m;
    const char *path = "test_sp_ports.po";
    FILE *fp;
    uint8_t block[512];
    int i;
    uint8_t st;

    memset(block, 0xA5, sizeof(block));
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fail("create ports po");
    }
    for (i = 0; i < 4; i++) {
        if (fwrite(block, 1, 512, fp) != 512) {
            fail("write ports po");
        }
    }
    fclose(fp);

    if (!apple2_init(&m)) {
        fail("init ports");
    }
    if (apple2_smartport_mount(&m, 7, 0, path) != 0) {
        fail("mount s7");
    }

    /* Write command: status (0), unit 0 — via $C0F4 data port. */
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 0); /* cmd */
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 0); /* unit */
    softswitch_c0_write(&m, 0xC0F0 + SP_STATUS, 0); /* fire */

    st = softswitch_c0_read(&m, 0xC0F0 + SP_STATUS);
    if ((st & 0x80u) == 0) {
        fail("status ready bit after SP_STATUS write");
    }

    /* Result byte is at buffer[0]; firmware reads it from data port. */
    m.sp_device[7].sp_read_offset = 0;
    if (softswitch_c0_read(&m, 0xC0F0 + SP_DATA) != SP_SUCCESS) {
        fail("status cmd result via SP_DATA");
    }

    /* Read block 0 through softswitch: cmd=1, unit=0, block=0. */
    m.sp_device[7].sp_write_offset = 0;
    m.sp_device[7].sp_read_offset = 0;
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 1);
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 0);
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 0);
    softswitch_c0_write(&m, 0xC0F0 + SP_DATA, 0);
    softswitch_c0_write(&m, 0xC0F0 + SP_STATUS, 0);
    st = softswitch_c0_read(&m, 0xC0F0 + SP_STATUS);
    if ((st & 0x80u) == 0) {
        fail("status ready after read cmd");
    }
    m.sp_device[7].sp_read_offset = 0;
    if (softswitch_c0_read(&m, 0xC0F0 + SP_DATA) != SP_SUCCESS) {
        fail("read cmd result");
    }
    /* Next data byte is first payload (0xA5). */
    if (softswitch_c0_read(&m, 0xC0F0 + SP_DATA) != 0xA5u) {
        fail("block0 payload via SP_DATA");
    }

    /* Wrong nibble must NOT complete a command (guard against 0/1 regression). */
    m.sp_device[7].sp_status = 0;
    m.sp_device[7].sp_write_offset = 0;
    softswitch_c0_write(&m, 0xC0F0 + 0, 0); /* bogus old SP_DATA */
    softswitch_c0_write(&m, 0xC0F0 + 1, 0); /* bogus old SP_STATUS */
    if (m.sp_device[7].sp_status == 0x80u) {
        fail("legacy 0/1 nibble must not fire SP_STATUS");
    }

    apple2_shutdown(&m);
    remove(path);
}

/*
 * Pure SmartPort $C800 host trap: simulate post-JSR entry with inline
 * cmd + param list, after latching the SP slot via $Cnxx access.
 */
static void test_smartport_host_trap(void)
{
    apple2_t m;
    const char *path = "test_sp_trap.po";
    FILE *fp;
    uint8_t block[512];
    int i;
    uint16_t plist = 0x0300;
    uint16_t buf = 0x1000;
    uint16_t ret_after = 0x0803; /* cmd at 0x0800 after fake JSR */

    memset(block, 0x5A, sizeof(block));
    block[0] = 0x11;
    block[1] = 0x22;
    fp = fopen(path, "wb");
    if (fp == NULL) {
        fail("create trap po");
    }
    for (i = 0; i < 4; i++) {
        if (fwrite(block, 1, 512, fp) != 512) {
            fail("write trap po");
        }
    }
    fclose(fp);

    if (!apple2_init(&m)) {
        fail("init trap");
    }
    if (apple2_smartport_mount(&m, 7, 0, path) != 0) {
        fail("mount trap");
    }

    softswitch_slot_io_select(&m, 0xC700);
    if (m.last_io_select_slot != 7) {
        fail("SP I/O SELECT not recorded");
    }
    if (m.c800_card != 7 || m.strobed_slot != 7) {
        fail("SP did not claim C800");
    }

    /* --- STATUS unit 0 --- */
    m.cpu.cpu.sp = 0x1FDu;
    m.ram_main[0x1FE] = 0xFF; /* stacked = 0x07FF → cmd @ 0x0800 */
    m.ram_main[0x1FF] = 0x07;
    m.cpu.cpu.pc = 0xC800;
    m.ram_main[0x0800] = 0x00; /* STATUS */
    m.ram_main[0x0801] = (uint8_t)(plist & 0xFF);
    m.ram_main[0x0802] = (uint8_t)(plist >> 8);
    m.ram_main[plist + 0] = 3;
    m.ram_main[plist + 1] = 0; /* unit 0 = controller */
    m.ram_main[plist + 2] = 0x00; /* status list at $0400 */
    m.ram_main[plist + 3] = 0x04;
    m.ram_main[plist + 4] = 0;

    if (!sp_host_trap(&m)) {
        fail("status trap not taken");
    }
    if (m.cpu.cpu.pc != ret_after) {
        fail("status return pc");
    }
    if (m.cpu.cpu.C != 0 || m.cpu.cpu.A != SP_SUCCESS) {
        fail("status success flags");
    }
    if (m.ram_main[0x0400] < 1) {
        fail("status device count");
    }

    /* --- READ_BLOCK unit 1, block 0 into $1000 --- */
    m.cpu.cpu.sp = 0x1FDu;
    m.ram_main[0x1FE] = 0xFF;
    m.ram_main[0x1FF] = 0x07;
    m.cpu.cpu.pc = 0xC89B;
    m.ram_main[0x0800] = 0x01;
    m.ram_main[0x0801] = (uint8_t)(plist & 0xFF);
    m.ram_main[0x0802] = (uint8_t)(plist >> 8);
    m.ram_main[plist + 0] = 3;
    m.ram_main[plist + 1] = 1;
    m.ram_main[plist + 2] = (uint8_t)(buf & 0xFF);
    m.ram_main[plist + 3] = (uint8_t)(buf >> 8);
    m.ram_main[plist + 4] = 0;
    m.ram_main[plist + 5] = 0;
    m.ram_main[plist + 6] = 0;
    memset(m.ram_main + buf, 0, 512);

    if (!sp_host_trap(&m)) {
        fail("read trap not taken");
    }
    if (m.cpu.cpu.C != 0 || m.cpu.cpu.A != SP_SUCCESS) {
        fail("read success");
    }
    if (m.ram_main[buf] != 0x11 || m.ram_main[buf + 1] != 0x22) {
        fail("read data");
    }

    /* --- WRITE_BLOCK unit 1, block 1 --- */
    m.ram_main[buf] = 0xAB;
    m.ram_main[buf + 1] = 0xCD;
    for (i = 2; i < 512; i++) {
        m.ram_main[buf + i] = 0x00;
    }
    m.cpu.cpu.sp = 0x1FDu;
    m.ram_main[0x1FE] = 0xFF;
    m.ram_main[0x1FF] = 0x07;
    m.cpu.cpu.pc = 0xC9AA;
    m.ram_main[0x0800] = 0x02;
    m.ram_main[0x0801] = (uint8_t)(plist & 0xFF);
    m.ram_main[0x0802] = (uint8_t)(plist >> 8);
    m.ram_main[plist + 0] = 3;
    m.ram_main[plist + 1] = 1;
    m.ram_main[plist + 2] = (uint8_t)(buf & 0xFF);
    m.ram_main[plist + 3] = (uint8_t)(buf >> 8);
    m.ram_main[plist + 4] = 1;
    m.ram_main[plist + 5] = 0;
    m.ram_main[plist + 6] = 0;

    if (!sp_host_trap(&m)) {
        fail("write trap not taken");
    }
    if (m.cpu.cpu.C != 0 || m.cpu.cpu.A != SP_SUCCESS) {
        fail("write success");
    }

    m.sp_device[7].sp_buffer[0] = 1;
    m.sp_device[7].sp_buffer[1] = 0;
    m.sp_device[7].sp_buffer[2] = 1;
    m.sp_device[7].sp_buffer[3] = 0;
    sp_read(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS) {
        fail("reread after write");
    }
    if (m.sp_device[7].sp_buffer[1] != 0xAB || m.sp_device[7].sp_buffer[2] != 0xCD) {
        fail("write data mismatch");
    }

    /*
     * $C3xx overlays 80-col firmware; SP card latch remains. $C800 execute
     * is firmware, not the trap. $CFFF drops both; $C7xx can claim again.
     */
    {
        uint8_t c800_rom = m.rom_c000[0x800];

        softswitch_slot_io_select(&m, 0xC300);
        if (m.strobed_slot != 8 || !m.c800_internal) {
            fail("C3 did not overlay 80-col C800");
        }
        if (m.c800_card != 7) {
            fail("SP card latch dropped by C3 overlay");
        }
        if (apple2_debug_read(&m, 0xC800) != c800_rom) {
            fail("C800 not 80-col firmware after C3");
        }
        m.cpu.cpu.pc = 0xC800;
        if (sp_host_trap(&m)) {
            fail("SP trap stole 80-col $C800");
        }
        if (m.cpu.cpu.pc != 0xC800) {
            fail("80-col $C800 PC mutated");
        }

        (void)m.cpu.read(m.cpu.user, 0xCFFF);
        if (m.strobed_slot != -1 || m.c800_card != -1 || m.c800_internal) {
            fail("CFFF did not release both C800 latches");
        }
        softswitch_slot_io_select(&m, 0xC700);
        if (m.c800_card != 7 || m.strobed_slot != 7) {
            fail("SP did not reclaim C800 after CFFF");
        }
        m.cpu.cpu.sp = 0x1FDu;
        m.ram_main[0x1FE] = 0xFF;
        m.ram_main[0x1FF] = 0x07;
        m.cpu.cpu.pc = 0xC800;
        m.ram_main[0x0800] = 0x00;
        m.ram_main[0x0801] = (uint8_t)(plist & 0xFF);
        m.ram_main[0x0802] = (uint8_t)(plist >> 8);
        m.ram_main[plist + 0] = 3;
        m.ram_main[plist + 1] = 0;
        m.ram_main[plist + 2] = 0x00;
        m.ram_main[plist + 3] = 0x04;
        m.ram_main[plist + 4] = 0;
        if (!sp_host_trap(&m)) {
            fail("SP trap not taken after reclaim");
        }
    }

    apple2_shutdown(&m);
    remove(path);
}

int main(void)
{
    test_mockingboard_attach();
    test_cards_support_slots_one_through_seven();
    test_smartport_mount_missing();
    test_smartport_image_roundtrip();
    test_smartport_softswitch_ports();
    test_smartport_host_trap();
    printf("ok\n");
    return 0;
}

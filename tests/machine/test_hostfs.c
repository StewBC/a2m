/* HostFS unit tests — NAPS parse, ProDOS map, SmartPort mount/read. */
#include "apple2.h"
#include "hostfs.h"
#include "smrtprt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef A2M_FIXTURE_DIR
#define A2M_FIXTURE_DIR "tests/fixtures"
#endif

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void test_naps_and_mangle(void)
{
    char name[16];
    uint8_t type = 0;
    uint16_t aux = 0;

    if (!hostfs_naps_parse_name("PRODOS#FF0000", name, sizeof(name), &type, &aux)) {
        fail("parse PRODOS NAPS");
    }
    if (strcmp(name, "PRODOS") != 0 || type != 0xFFu || aux != 0x0000u) {
        fail("PRODOS NAPS fields");
    }
    if (!hostfs_naps_parse_name("HELLO#060800", name, sizeof(name), &type, &aux)) {
        fail("parse HELLO NAPS");
    }
    if (strcmp(name, "HELLO") != 0 || type != 0x06u || aux != 0x0800u) {
        fail("HELLO NAPS fields");
    }
    if (hostfs_naps_parse_name("readme.txt", name, sizeof(name), &type, &aux)) {
        fail("non-NAPS should reject");
    }
    if (!hostfs_mangle_prodos_name("basic.system", name, sizeof(name)) ||
        strcmp(name, "BASIC.SYSTEM") != 0) {
        fail("mangle basic.system");
    }
    if (hostfs_mangle_prodos_name("123bad", name, sizeof(name))) {
        fail("name must start with letter");
    }
}

static void test_volume_map(void)
{
    char path[1024];
    hostfs_volume *vol;
    uint8_t block[512];
    uint8_t type;
    uint16_t key;
    uint16_t aux;
    int found_prodos = 0;
    int found_readme = 0;
    int i;

    snprintf(path, sizeof(path), "%s/hostfs", A2M_FIXTURE_DIR);
    vol = hostfs_mount(path, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("hostfs_mount fixture");
    }
    if (hostfs_total_blocks(vol) != 65535u) {
        fail("total_blocks");
    }
    if (strcmp(hostfs_volume_name(vol), "HOSTFS.S7D0") != 0) {
        fail("volume name");
    }
    /* At least PRODOS, HELLO, DATA — not readme.txt */
    if (hostfs_file_count(vol) < 3) {
        fprintf(stderr, "file_count=%d\n", hostfs_file_count(vol));
        fail("expected NAPS files");
    }

    if (hostfs_read_block(vol, 0, block) != 0) {
        fail("read boot");
    }
    if (block[0] != 0x01 || block[1] != 0x38) {
        fail("boot signature");
    }

    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read dir");
    }
    if ((block[4] >> 4) != 0x0F) {
        fail("volume header storage type");
    }
    if ((block[4] & 0x0F) != (uint8_t)strlen("HOSTFS.S7D0")) {
        fail("volume name length");
    }
    if (memcmp(block + 5, "HOSTFS.S7D0", 10) != 0) {
        fail("volume name in dir");
    }

    /* Scan first directory block file entries for PRODOS; ensure no README. */
    for (i = 1; i < 13; ++i) {
        uint8_t *e = block + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];

        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, "PRODOS") == 0) {
            found_prodos = 1;
            type = e[0x10];
            key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            aux = (uint16_t)(e[0x1F] | (e[0x20] << 8));
            if (type != 0xFFu || aux != 0x0000u) {
                fail("PRODOS type/aux");
            }
            if ((e[0] >> 4) != 0x02) {
                fail("PRODOS should be sapling");
            }
            if (key == 0) {
                fail("PRODOS key block");
            }
        }
        if (strcmp(nm, "README") == 0 || strcmp(nm, "README.TXT") == 0) {
            found_readme = 1;
        }
    }
    if (!found_prodos) {
        fail("PRODOS entry missing");
    }
    if (found_readme) {
        fail("non-NAPS readme should not appear");
    }

    /* Seedling HELLO: one data block equals key. */
    {
        int found_hello = 0;
        for (i = 1; i < 13; ++i) {
            uint8_t *e = block + 4 + i * 39;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            uint8_t data[512];

            if (e[0] == 0) {
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            if (strcmp(nm, "HELLO") != 0) {
                continue;
            }
            found_hello = 1;
            if ((e[0] >> 4) != 0x01) {
                fail("HELLO seedling");
            }
            key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            if (hostfs_read_block(vol, key, data) != 0) {
                fail("read HELLO data");
            }
            if (data[0] != 0xA9 || data[1] != 0x00 || data[2] != 0x60) {
                fail("HELLO payload");
            }
        }
        if (!found_hello) {
            fail("HELLO missing");
        }
    }

    /* Sapling DATA 1024 bytes → storage type 2, index + 2 data. */
    {
        int found_data = 0;
        for (i = 1; i < 13; ++i) {
            uint8_t *e = block + 4 + i * 39;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            uint8_t index[512];
            uint8_t data[512];
            uint16_t db0;

            if (e[0] == 0) {
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            if (strcmp(nm, "DATA") != 0) {
                continue;
            }
            found_data = 1;
            if ((e[0] >> 4) != 0x02) {
                fail("DATA sapling");
            }
            key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            if (hostfs_read_block(vol, key, index) != 0) {
                fail("read DATA index");
            }
            db0 = (uint16_t)(index[0] | (index[256] << 8));
            if (db0 == 0 || hostfs_read_block(vol, db0, data) != 0) {
                fail("read DATA block0");
            }
            if (data[0] != 0x00 || data[1] != 0x01 || data[255] != 0xFF) {
                fail("DATA payload");
            }
        }
        if (!found_data) {
            fail("DATA missing");
        }
    }

    hostfs_eject(vol);
}

static void test_smartport_hostfs_and_mixed(void)
{
    apple2_t m;
    char host_path[1024];
    const char *img_path = "test_hostfs_mixed.po";
    FILE *fp;
    uint8_t blank[512];
    int i;

    snprintf(host_path, sizeof(host_path), "%s/hostfs", A2M_FIXTURE_DIR);

    memset(blank, 0, sizeof(blank));
    fp = fopen(img_path, "wb");
    if (fp == NULL) {
        fail("create mixed po");
    }
    for (i = 0; i < 4; ++i) {
        if (fwrite(blank, 1, 512, fp) != 512) {
            fail("write mixed po");
        }
    }
    fclose(fp);

    if (!apple2_init(&m)) {
        fail("init");
    }
    if (apple2_smartport_mount(&m, 7, 0, host_path) != 0) {
        fail("mount hostfs s7d0");
    }
    if (apple2_smartport_mount(&m, 7, 1, img_path) != 0) {
        fail("mount image s7d1");
    }
    if (!sp_unit_mounted(&m.sp_device[7], 0) ||
        m.sp_device[7].backend[0] != SP_BACKEND_HOSTFS) {
        fail("unit0 hostfs");
    }
    if (!sp_unit_mounted(&m.sp_device[7], 1) ||
        m.sp_device[7].backend[1] != SP_BACKEND_IMAGE) {
        fail("unit1 image");
    }

    m.sp_device[7].sp_buffer[0] = 0;
    m.sp_device[7].sp_buffer[1] = 0; /* device 0 */
    sp_status(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS) {
        fail("hostfs status");
    }
    if (m.sp_device[7].sp_buffer[1] != 0xFF || m.sp_device[7].sp_buffer[2] != 0xFF) {
        fprintf(stderr, "blocks %u %u\n",
            m.sp_device[7].sp_buffer[1], m.sp_device[7].sp_buffer[2]);
        fail("hostfs block count 65535");
    }

    m.sp_device[7].sp_buffer[1] = 0;
    m.sp_device[7].sp_buffer[2] = 0;
    m.sp_device[7].sp_buffer[3] = 0;
    sp_read(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS) {
        fail("hostfs read block0");
    }
    if (m.sp_device[7].sp_buffer[1] != 0x01 || m.sp_device[7].sp_buffer[2] != 0x38) {
        fail("hostfs boot bytes");
    }

    /* WRITE → write-protect */
    m.sp_device[7].sp_buffer[1] = 0;
    m.sp_device[7].sp_buffer[2] = 0;
    m.sp_device[7].sp_buffer[3] = 0;
    memset(&m.sp_device[7].sp_buffer[4], 0xA5, 512);
    sp_write(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_WRITE_PROTECT) {
        fail("hostfs write protect");
    }

    /* Image unit still works. */
    m.sp_device[7].sp_buffer[1] = (uint8_t)(1 << 7);
    m.sp_device[7].sp_buffer[2] = 0;
    m.sp_device[7].sp_buffer[3] = 0;
    sp_status(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS ||
        m.sp_device[7].sp_buffer[1] != 4) {
        fail("image unit status");
    }

    if (apple2_smartport_eject(&m, 7, 0) != 0 ||
        sp_unit_mounted(&m.sp_device[7], 0)) {
        fail("eject hostfs");
    }
    if (apple2_smartport_eject(&m, 7, 1) != 0 ||
        sp_unit_mounted(&m.sp_device[7], 1)) {
        fail("eject image");
    }

    apple2_shutdown(&m);
    remove(img_path);
}

int main(void)
{
    test_naps_and_mangle();
    test_volume_map();
    test_smartport_hostfs_and_mixed();
    printf("hostfs tests ok\n");
    return 0;
}

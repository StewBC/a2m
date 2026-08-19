/* HostFS unit tests — NAPS parse, ProDOS map, SmartPort mount/read/write. */
#include "apple2.h"
#include "hostfs.h"
#include "smrtprt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define HOSTFS_MKDIR(path) _mkdir(path)
#else
#include <unistd.h>
#define HOSTFS_MKDIR(path) mkdir(path, 0755)
#endif

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
    char naps[64];
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

    /* Assembler-style already-tagged name must not double-tag. */
    if (!hostfs_compose_naps_filename("GAME#060800", 0x06u, 0x0800u, naps, sizeof(naps)) ||
        strcmp(naps, "GAME#060800") != 0) {
        fail("compose already-tagged");
    }
    if (!hostfs_compose_naps_filename("GAME#060800", 0xFCu, 0x0801u, naps, sizeof(naps)) ||
        strcmp(naps, "GAME#FC0801") != 0) {
        fail("compose retag from NAPS stem");
    }
    if (!hostfs_compose_naps_filename("PLAIN", 0x06u, 0x2000u, naps, sizeof(naps)) ||
        strcmp(naps, "PLAIN#062000") != 0) {
        fail("compose plain name");
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

    /* WRITE boot block (RAM meta) must succeed now. */
    m.sp_device[7].sp_buffer[1] = 0;
    m.sp_device[7].sp_buffer[2] = 0;
    m.sp_device[7].sp_buffer[3] = 0;
    memset(&m.sp_device[7].sp_buffer[4], 0xA5, 512);
    /* Restore a valid boot signature so later reads still look sane if needed. */
    m.sp_device[7].sp_buffer[4] = 0x01;
    m.sp_device[7].sp_buffer[5] = 0x38;
    sp_write(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS) {
        fail("hostfs write meta");
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

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL || fwrite(data, 1, len, fp) != len) {
        fail("write_file");
    }
    fclose(fp);
}

static void test_write_through_and_rescan(void)
{
    const char *dir = "test_hostfs_rw_dir";
    char path[512];
    hostfs_volume *vol;
    uint8_t block[512];
    uint8_t hello_key_payload[3] = {0xA9, 0x01, 0x60};
    uint16_t key = 0;
    int i;
    FILE *fp;
    uint8_t got[8];

    HOSTFS_MKDIR(dir);
    snprintf(path, sizeof(path), "%s/HELLO#060800", dir);
    write_file(path, hello_key_payload, sizeof(hello_key_payload));

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("mount rw dir");
    }

    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read dir for HELLO key");
    }
    for (i = 1; i < 13; ++i) {
        uint8_t *e = block + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, "HELLO") == 0) {
            key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            break;
        }
    }
    if (key == 0) {
        fail("HELLO key missing");
    }

    memset(block, 0, sizeof(block));
    block[0] = 0xEA;
    block[1] = 0xEA;
    block[2] = 0x60;
    if (hostfs_write_block(vol, key, block) != 0) {
        fail("write HELLO data");
    }

    fp = fopen(path, "rb");
    if (fp == NULL || fread(got, 1, 3, fp) != 3) {
        fail("reread HELLO host");
    }
    fclose(fp);
    if (got[0] != 0xEA || got[1] != 0xEA || got[2] != 0x60) {
        fail("host write-through bytes");
    }

    /* Phase 2: grow host file and rescan EOF. */
    {
        uint8_t bigger[20];
        uint8_t dirblk[512];
        uint32_t eof = 0;
        memset(bigger, 0x55, sizeof(bigger));
        write_file(path, bigger, sizeof(bigger));
        if (hostfs_rescan(vol) != 0) {
            fail("rescan after grow");
        }
        if (hostfs_read_block(vol, 2, dirblk) != 0) {
            fail("read dir after rescan");
        }
        for (i = 1; i < 13; ++i) {
            uint8_t *e = dirblk + 4 + i * 39;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            if (e[0] == 0) {
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            if (strcmp(nm, "HELLO") == 0) {
                eof = (uint32_t)e[0x15] | ((uint32_t)e[0x16] << 8) |
                      ((uint32_t)e[0x17] << 16);
                break;
            }
        }
        if (eof != 20u) {
            fprintf(stderr, "eof=%u\n", eof);
            fail("rescan EOF");
        }
    }

    /* Phase 2 add: new NAPS file appears. */
    snprintf(path, sizeof(path), "%s/NEW#040000", dir);
    write_file(path, "hi", 2);
    if (hostfs_rescan(vol) != 0) {
        fail("rescan add");
    }
    {
        int found = 0;
        uint8_t dirblk[512];
        if (hostfs_read_block(vol, 2, dirblk) != 0) {
            fail("dir after add");
        }
        for (i = 1; i < 13; ++i) {
            uint8_t *e = dirblk + 4 + i * 39;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            if (e[0] == 0) {
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            if (strcmp(nm, "NEW") == 0) {
                found = 1;
            }
        }
        if (!found) {
            fail("NEW not in catalog after rescan");
        }
    }

    hostfs_eject(vol);
    remove("test_hostfs_rw_dir/HELLO#060800");
    remove("test_hostfs_rw_dir/NEW#040000");
    remove("test_hostfs_rw_dir/" HOSTFS_ORDER_FILENAME);
    rmdir(dir);
}

static void test_create_reconcile(void)
{
    const char *dir = "test_hostfs_create_dir";
    hostfs_volume *vol;
    uint8_t dirblk[512];
    uint8_t data[512];
    uint16_t new_key;
    char path[512];
    struct stat st;
    int i;

    HOSTFS_MKDIR(dir);
    /* Seed with one file so mount builds a writable volume with spare dir slots. */
    snprintf(path, sizeof(path), "%s/SEED#060000", dir);
    write_file(path, "x", 1);

    vol = hostfs_mount(dir, "HOSTFS.S5D0");
    if (vol == NULL) {
        fail("mount create dir");
    }

    /* Allocate an orphan data block via write, then publish a dir entry for it. */
    new_key = 100;
    memset(data, 0xCC, sizeof(data));
    if (hostfs_write_block(vol, new_key, data) != 0) {
        fail("orphan data write");
    }

    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read dir for create");
    }
    /* Find a free entry slot (skip volume header). */
    for (i = 1; i < 13; ++i) {
        uint8_t *e = dirblk + 4 + i * 39;
        if (e[0] != 0) {
            continue;
        }
        memset(e, 0, 39);
        e[0] = (uint8_t)((1u << 4) | 4u); /* seedling, name len 4 */
        memcpy(e + 1, "TEST", 4);
        e[0x10] = 0x06;
        e[0x11] = (uint8_t)(new_key & 0xFFu);
        e[0x12] = (uint8_t)((new_key >> 8) & 0xFFu);
        e[0x13] = 1;
        e[0x15] = 3; /* eof 3 */
        e[0x1E] = 0xC3;
        e[0x1F] = 0x00;
        e[0x20] = 0x08; /* aux $0800 */
        e[0x25] = 2; /* header pointer */
        break;
    }
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("dir write create");
    }

    snprintf(path, sizeof(path), "%s/TEST#060800", dir);
    if (stat(path, &st) != 0) {
        fail("CREATE did not make NAPS host file");
    }

    hostfs_eject(vol);
    remove("test_hostfs_create_dir/SEED#060000");
    remove("test_hostfs_create_dir/GAME#060800");
    remove("test_hostfs_create_dir/" HOSTFS_ORDER_FILENAME);
    rmdir(dir);
}

static void read_catalog_names(hostfs_volume *vol, char names[][16], int *count, int max)
{
    uint8_t dirblk[512];
    int i;
    *count = 0;
    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read catalog");
    }
    for (i = 1; i < 13 && *count < max; ++i) {
        uint8_t *e = dirblk + 4 + i * 39;
        uint8_t nl;
        if (e[0] == 0) {
            continue;
        }
        nl = e[0] & 0x0Fu;
        memcpy(names[*count], e + 1, nl);
        names[*count][nl] = '\0';
        (*count)++;
    }
}

static void test_order_manifest(void)
{
    const char *dir = "test_hostfs_order_dir";
    char path[512];
    hostfs_volume *vol;
    char names[8][16];
    int count = 0;
    FILE *fp;
    uint8_t dirblk[512];
    int i;
    uint8_t *slot_a = NULL;
    uint8_t *slot_c = NULL;
    uint8_t tmp[39];

    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_order_dir/ALPHA#060000", "a", 1);
    write_file("test_hostfs_order_dir/BETA#060000", "b", 1);
    write_file("test_hostfs_order_dir/GAMMA#060000", "c", 1);

    /* Manifest forces GAMMA, ALPHA, then unlisted BETA appends. */
    snprintf(path, sizeof(path), "%s/%s", dir, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "w");
    if (fp == NULL) {
        fail("write order file");
    }
    fprintf(fp, "# comment\nGAMMA#060000\nALPHA#060000\nMISSING#040000\n");
    fclose(fp);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("mount order dir");
    }
    read_catalog_names(vol, names, &count, 8);
    if (count != 3 || strcmp(names[0], "GAMMA") != 0 || strcmp(names[1], "ALPHA") != 0 ||
        strcmp(names[2], "BETA") != 0) {
        fprintf(stderr, "order got");
        for (i = 0; i < count; ++i) {
            fprintf(stderr, " %s", names[i]);
        }
        fprintf(stderr, "\n");
        fail("manifest mount order");
    }

    /* Swap ALPHA and GAMMA in the directory (CAT.DOCTOR-style) and write back. */
    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read dir for swap");
    }
    for (i = 1; i < 13; ++i) {
        uint8_t *e = dirblk + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, "GAMMA") == 0) {
            slot_c = e;
        }
        if (strcmp(nm, "ALPHA") == 0) {
            slot_a = e;
        }
    }
    if (slot_a == NULL || slot_c == NULL) {
        fail("swap slots");
    }
    memcpy(tmp, slot_a, 39);
    memcpy(slot_a, slot_c, 39);
    memcpy(slot_c, tmp, 39);
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("write swapped dir");
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        fail("order file missing after reorder");
    }
    {
        char got[3][64];
        int n = 0;
        char line[128];
        while (n < 3 && fgets(line, sizeof(line), fp) != NULL) {
            size_t len;
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
                continue;
            }
            len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            snprintf(got[n], sizeof(got[n]), "%s", line);
            n++;
        }
        fclose(fp);
        if (n != 3 || strcmp(got[0], "ALPHA#060000") != 0 ||
            strcmp(got[1], "GAMMA#060000") != 0 || strcmp(got[2], "BETA#060000") != 0) {
            fprintf(stderr, "manifest lines: %s / %s / %s\n",
                n > 0 ? got[0] : "", n > 1 ? got[1] : "", n > 2 ? got[2] : "");
            fail("manifest not updated on reorder");
        }
    }

    hostfs_eject(vol);

    /* Remount must keep the new order from hostfs.order. */
    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("remount order dir");
    }
    read_catalog_names(vol, names, &count, 8);
    if (count != 3 || strcmp(names[0], "ALPHA") != 0 || strcmp(names[1], "GAMMA") != 0 ||
        strcmp(names[2], "BETA") != 0) {
        fail("remount preserved order");
    }
    hostfs_eject(vol);

    remove("test_hostfs_order_dir/ALPHA#060000");
    remove("test_hostfs_order_dir/BETA#060000");
    remove("test_hostfs_order_dir/GAMMA#060000");
    remove("test_hostfs_order_dir/" HOSTFS_ORDER_FILENAME);
    rmdir(dir);
}

int main(void)
{
    test_naps_and_mangle();
    test_volume_map();
    test_smartport_hostfs_and_mixed();
    test_write_through_and_rescan();
    test_create_reconcile();
    test_order_manifest();
    printf("hostfs tests ok\n");
    return 0;
}

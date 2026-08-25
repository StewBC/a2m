/* HostFS unit tests — NAPS parse, ProDOS map, SmartPort mount/read/write. */
#include "apple2.h"
#include "fs_watch.h"
#include "hostfs.h"
#include "smrtprt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
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

static void wipe_tree(const char *path)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    (void)system(cmd);
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

    wipe_tree(dir);
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
    wipe_tree(dir);
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

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    /* Seed with one file so mount builds a writable volume with spare dir slots. */
    snprintf(path, sizeof(path), "%s/SEED#060000", dir);
    write_file(path, "x", 1);

    vol = hostfs_mount(dir, "HOSTFS.S5D0");
    if (vol == NULL) {
        fail("mount create dir");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);

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
    /* The deferred internal invalidation and the watcher's later echo collapse
       to one authoritative parent scan and must not duplicate the node. */
    if (!hostfs_test_inject_event(vol, FS_WATCH_CREATE, "TEST#060800")) {
        fail("inject guest create echo");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_file_count(vol) != 2 ||
        hostfs_test_targeted_directory_scans(vol) != 1 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("guest create echo must reconcile once without duplicating");
    }

    /* A guest block write extends the physical host file to a whole block, but
       the ProDOS directory EOF remains authoritative. Its modify echo must not
       turn the three-byte guest file into a 512-byte catalog entry. */
    memset(data, 0xA7, sizeof(data));
    if (hostfs_write_block(vol, new_key, data) != 0 ||
        !hostfs_test_inject_event(vol, FS_WATCH_MODIFY, "TEST#060800")) {
        fail("inject guest data-write echo");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read catalog after data-write echo");
    }
    for (i = 1; i < HOSTFS_ENTRIES_PER_BLOCK; ++i) {
        const uint8_t *e = dirblk + 4 + i * HOSTFS_ENTRY_LENGTH;
        uint8_t nl = (uint8_t)(e[0] & 0x0Fu);
        char name[HOSTFS_NAME_MAX];
        uint32_t eof;
        if (e[0] == 0) {
            continue;
        }
        memcpy(name, e + 1, nl);
        name[nl] = '\0';
        if (strcmp(name, "TEST") != 0) {
            continue;
        }
        eof = (uint32_t)e[0x15] | ((uint32_t)e[0x16] << 8) |
              ((uint32_t)e[0x17] << 16);
        if (eof != 3u) {
            fail("guest write echo changed directory-authoritative EOF");
        }
        break;
    }
    if (i == HOSTFS_ENTRIES_PER_BLOCK ||
        hostfs_test_targeted_file_stats(vol) != 1 ||
        hostfs_file_count(vol) != 2) {
        fail("guest data-write echo was not idempotent");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
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

    wipe_tree(dir);
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

    wipe_tree(dir);
}

static int find_entry_in_dir_block(
    const uint8_t *block, const char *name, uint8_t *out_st, uint16_t *out_key, uint8_t *out_type)
{
    int i;
    for (i = 1; i < 13; ++i) {
        const uint8_t *e = block + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, name) == 0) {
            if (out_st != NULL) {
                *out_st = (uint8_t)(e[0] >> 4);
            }
            if (out_key != NULL) {
                *out_key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            }
            if (out_type != NULL) {
                *out_type = e[0x10];
            }
            return 1;
        }
    }
    /* continuation blocks start at slot 0 */
    for (i = 0; i < 13; ++i) {
        const uint8_t *e = block + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, name) == 0) {
            if (out_st != NULL) {
                *out_st = (uint8_t)(e[0] >> 4);
            }
            if (out_key != NULL) {
                *out_key = (uint16_t)(e[0x11] | (e[0x12] << 8));
            }
            if (out_type != NULL) {
                *out_type = e[0x10];
            }
            return 1;
        }
    }
    return 0;
}

/* Walk a subdirectory's linked block chain. The first block reserves slot 0
   for the subdirectory header; continuation blocks use every slot. */
static int subdir_catalog(
    hostfs_volume *vol,
    uint16_t first_block,
    const char *want,
    int *out_count,
    uint16_t *out_key)
{
    uint16_t block = first_block;
    unsigned guard = 0;
    int count = 0;
    int found = 0;

    while (block != 0u && guard++ < HOSTFS_MAX_NODES) {
        uint8_t data[HOSTFS_BLOCK_SIZE];
        uint16_t next;
        int slot;
        int start = block == first_block ? 1 : 0;

        if (hostfs_read_block(vol, block, data) != 0) {
            fail("read subdirectory chain");
        }
        next = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            const uint8_t *e = data + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t nl;
            char name[HOSTFS_NAME_MAX];

            if (e[0] == 0) {
                continue;
            }
            nl = (uint8_t)(e[0] & 0x0Fu);
            memcpy(name, e + 1, nl);
            name[nl] = '\0';
            count++;
            if (want != NULL && strcmp(name, want) == 0) {
                found = 1;
                if (out_key != NULL) {
                    *out_key = (uint16_t)(e[0x11] | ((uint16_t)e[0x12] << 8));
                }
            }
        }
        block = next;
    }
    if (block != 0u) {
        fail("subdirectory chain cycle");
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found;
}

static void make_large_numbered_files(const char *dir, char prefix, int first, int last)
{
    int i;
    for (i = first; i <= last; ++i) {
        char path[512];
        uint8_t payload[4];
        snprintf(path, sizeof(path), "%s/%c%04d#060000", dir, prefix, i);
        payload[0] = (uint8_t)prefix;
        payload[1] = (uint8_t)(i & 0xFF);
        payload[2] = (uint8_t)((i >> 8) & 0xFF);
        payload[3] = (uint8_t)(prefix ^ i);
        write_file(path, payload, sizeof(payload));
    }
}

static void remove_large_numbered_files(const char *dir, char prefix, int first, int last)
{
    int i;
    for (i = first; i <= last; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%c%04d#060000", dir, prefix, i);
        if (remove(path) != 0) {
            fail("remove numbered HostFS file");
        }
    }
}

static void test_nested_directories(void)
{
    char path[1024];
    hostfs_volume *vol;
    uint8_t block[512];
    uint8_t st = 0;
    uint8_t type = 0;
    uint16_t utils_key = 0;
    uint16_t nested_key = 0;
    uint16_t tool_key = 0;
    uint8_t data[512];

    snprintf(path, sizeof(path), "%s/hostfs", A2M_FIXTURE_DIR);
    vol = hostfs_mount(path, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("nested mount");
    }

    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read volume dir");
    }
    if (!find_entry_in_dir_block(block, "UTILS", &st, &utils_key, &type)) {
        fail("UTILS dir missing from volume");
    }
    if (st != 0x0Du || type != 0x0Fu || utils_key == 0) {
        fail("UTILS should be subdirectory storage/type");
    }
    if (find_entry_in_dir_block(block, "NOTES", &st, &utils_key, &type) ||
        find_entry_in_dir_block(block, "HIDDENDIR", &st, &utils_key, &type)) {
        fail("non-NAPS / dotdir should not appear at root");
    }

    if (hostfs_read_block(vol, utils_key, block) != 0) {
        fail("read UTILS key");
    }
    if ((block[4] >> 4) != 0x0Eu) {
        fail("UTILS subdirectory header storage $0E");
    }
    if (block[4 + 0x10] != 0x75) {
        fail("UTILS header marker $75");
    }
    if (!find_entry_in_dir_block(block, "TOOL", &st, &tool_key, &type)) {
        fail("TOOL missing in UTILS");
    }
    if (st != 0x01u || type != 0x06u) {
        fail("TOOL type/storage");
    }
    if (!find_entry_in_dir_block(block, "NESTED", &st, &nested_key, &type)) {
        fail("NESTED missing in UTILS");
    }
    if (st != 0x0Du || type != 0x0Fu) {
        fail("NESTED should be DIR");
    }
    if (find_entry_in_dir_block(block, "NOTES", &st, &tool_key, &type)) {
        fail("notes.txt must not appear in UTILS");
    }

    if (hostfs_read_block(vol, tool_key, data) != 0) {
        fail("read TOOL data");
    }
    if (memcmp(data, "tool", 4) != 0) {
        fail("TOOL payload");
    }

    if (hostfs_read_block(vol, nested_key, block) != 0) {
        fail("read NESTED key");
    }
    if ((block[4] >> 4) != 0x0Eu) {
        fail("NESTED header");
    }
    {
        uint16_t deep_key = 0;
        if (!find_entry_in_dir_block(block, "DEEP", &st, &deep_key, &type)) {
            fail("DEEP missing");
        }
        if (hostfs_read_block(vol, deep_key, data) != 0 || memcmp(data, "deep", 4) != 0) {
            fail("DEEP payload");
        }
    }

    hostfs_eject(vol);
}

static void test_nested_rescan(void)
{
    const char *dir = "test_hostfs_nested_rescan";
    hostfs_volume *vol;
    uint8_t block[512];
    uint8_t st = 0;
    uint8_t type = 0;
    uint16_t sub_key = 0;
    char path[512];

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_nested_rescan/ROOT#060000", "r", 1);
    HOSTFS_MKDIR("test_hostfs_nested_rescan/SUB");
    write_file("test_hostfs_nested_rescan/SUB/IN#040000", "in", 2);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("nested rescan mount");
    }
    if (hostfs_read_block(vol, 2, block) != 0 ||
        !find_entry_in_dir_block(block, "SUB", &st, &sub_key, &type)) {
        fail("SUB at mount");
    }

    write_file("test_hostfs_nested_rescan/SUB/NEW#060800", "xy", 2);
    if (hostfs_rescan(vol) != 0) {
        fail("nested rescan");
    }
    if (hostfs_read_block(vol, sub_key, block) != 0 ||
        !find_entry_in_dir_block(block, "NEW", &st, &sub_key, &type)) {
        fail("NEW not visible after rescan");
    }

    HOSTFS_MKDIR("test_hostfs_nested_rescan/ADDED");
    if (hostfs_rescan(vol) != 0) {
        fail("rescan add dir");
    }
    if (hostfs_read_block(vol, 2, block) != 0 ||
        !find_entry_in_dir_block(block, "ADDED", &st, &sub_key, &type) || st != 0x0Du) {
        fail("ADDED dir missing after rescan");
    }

    remove("test_hostfs_nested_rescan/SUB/NEW#060800");
    if (hostfs_rescan(vol) != 0) {
        fail("rescan remove nested file");
    }
    /* Remount SUB key from volume dir (may be unchanged). */
    if (hostfs_read_block(vol, 2, block) != 0 ||
        !find_entry_in_dir_block(block, "SUB", &st, &sub_key, &type)) {
        fail("SUB after delete rescan");
    }
    if (hostfs_read_block(vol, sub_key, block) != 0 ||
        find_entry_in_dir_block(block, "NEW", &st, &sub_key, &type)) {
        fail("NEW should be gone");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
    (void)path;
}

static void fill_dir_entry(
    uint8_t *e,
    uint8_t storage_type,
    const char *name,
    uint8_t file_type,
    uint16_t key,
    uint16_t blocks_used,
    uint32_t eof,
    uint16_t aux,
    uint16_t header_ptr)
{
    size_t nlen = strlen(name);
    memset(e, 0, 39);
    e[0] = (uint8_t)((storage_type << 4) | (nlen & 0x0Fu));
    memcpy(e + 1, name, nlen);
    e[0x10] = file_type;
    e[0x11] = (uint8_t)(key & 0xFFu);
    e[0x12] = (uint8_t)((key >> 8) & 0xFFu);
    e[0x13] = (uint8_t)(blocks_used & 0xFFu);
    e[0x14] = (uint8_t)((blocks_used >> 8) & 0xFFu);
    e[0x15] = (uint8_t)(eof & 0xFFu);
    e[0x16] = (uint8_t)((eof >> 8) & 0xFFu);
    e[0x17] = (uint8_t)((eof >> 16) & 0xFFu);
    e[0x1E] = 0xC3;
    e[0x1F] = (uint8_t)(aux & 0xFFu);
    e[0x20] = (uint8_t)((aux >> 8) & 0xFFu);
    e[0x25] = (uint8_t)(header_ptr & 0xFFu);
    e[0x26] = (uint8_t)((header_ptr >> 8) & 0xFFu);
}

static void test_dir_write_through(void)
{
    const char *dir = "test_hostfs_dir_wt";
    hostfs_volume *vol;
    uint8_t dirblk[512];
    uint8_t subblk[512];
    uint8_t data[512];
    uint16_t games_key = 120;
    uint16_t file_key = 130;
    char path[512];
    struct stat st;
    int i;
    uint8_t stype = 0;
    uint8_t ftype = 0;
    uint16_t key = 0;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_dir_wt/SEED#060000", "x", 1);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("dir wt mount");
    }

    /* CREATE empty subdirectory key block with $0E header. */
    memset(subblk, 0, sizeof(subblk));
    subblk[4] = (uint8_t)((0x0Eu << 4) | 5u);
    memcpy(subblk + 5, "GAMES", 5);
    subblk[4 + 0x10] = 0x75;
    subblk[4 + 0x1E] = 0xC3;
    subblk[4 + 0x1F] = 39;
    subblk[4 + 0x20] = 13;
    if (hostfs_write_block(vol, games_key, subblk) != 0) {
        fail("write GAMES key");
    }

    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read vol dir");
    }
    for (i = 1; i < 13; ++i) {
        uint8_t *e = dirblk + 4 + i * 39;
        if (e[0] != 0) {
            continue;
        }
        fill_dir_entry(e, 0x0D, "GAMES", 0x0F, games_key, 1, 512, 0, 2);
        break;
    }
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("publish GAMES entry");
    }

    snprintf(path, sizeof(path), "%s/GAMES", dir);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fail("CREATE DIR did not mkdir GAMES");
    }

    /* CREATE seedling file inside GAMES. */
    memset(data, 0xAB, sizeof(data));
    data[0] = 'H';
    data[1] = 'I';
    if (hostfs_write_block(vol, file_key, data) != 0) {
        fail("write nested data");
    }
    if (hostfs_read_block(vol, games_key, subblk) != 0) {
        fail("read GAMES dir");
    }
    for (i = 1; i < 13; ++i) {
        uint8_t *e = subblk + 4 + i * 39;
        if (e[0] != 0) {
            continue;
        }
        fill_dir_entry(e, 0x01, "HI", 0x06, file_key, 1, 2, 0x0800, games_key);
        break;
    }
    /* bump active count in subdir header */
    subblk[4 + 0x21] = 1;
    subblk[4 + 0x22] = 0;
    if (hostfs_write_block(vol, games_key, subblk) != 0) {
        fail("publish HI in GAMES");
    }
    snprintf(path, sizeof(path), "%s/GAMES/HI#060800", dir);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fail("nested CREATE did not make NAPS file");
    }

    /* RENAME GAMES -> FUN */
    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read vol for rename");
    }
    if (!find_entry_in_dir_block(dirblk, "GAMES", &stype, &key, &ftype)) {
        fail("GAMES missing before rename");
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
        if (strcmp(nm, "GAMES") == 0) {
            memset(e + 1, 0, 15);
            memcpy(e + 1, "FUN", 3);
            e[0] = (uint8_t)((0x0Du << 4) | 3u);
            break;
        }
    }
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("rename GAMES->FUN");
    }
    if (stat("test_hostfs_dir_wt/FUN", &st) != 0 || !S_ISDIR(st.st_mode)) {
        fail("RENAME DIR did not rename host folder");
    }
    if (stat("test_hostfs_dir_wt/FUN/HI#060800", &st) != 0) {
        fail("child path not updated after dir rename");
    }
    if (stat("test_hostfs_dir_wt/GAMES", &st) == 0) {
        fail("old GAMES path still exists");
    }

    /* DESTROY nested file then directory. */
    if (hostfs_read_block(vol, 2, dirblk) != 0 ||
        !find_entry_in_dir_block(dirblk, "FUN", &stype, &key, &ftype)) {
        fail("FUN key lookup");
    }
    if (hostfs_read_block(vol, key, subblk) != 0) {
        fail("read FUN");
    }
    for (i = 1; i < 13; ++i) {
        uint8_t *e = subblk + 4 + i * 39;
        uint8_t nl = e[0] & 0x0Fu;
        char nm[16];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, "HI") == 0) {
            memset(e, 0, 39);
        }
    }
    subblk[4 + 0x21] = 0;
    if (hostfs_write_block(vol, key, subblk) != 0) {
        fail("destroy HI");
    }
    if (stat("test_hostfs_dir_wt/FUN/HI#060800", &st) == 0) {
        fail("DESTROY nested file left host file");
    }

    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read vol for destroy dir");
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
        if (strcmp(nm, "FUN") == 0) {
            memset(e, 0, 39);
        }
    }
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("destroy FUN");
    }
    if (stat("test_hostfs_dir_wt/FUN", &st) == 0) {
        fail("DESTROY DIR left host folder");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

/* Healthy notification mode does no scan or stat work without an event. A
   structural event is coalesced and reconciles only its immediate parent. */
static void test_touch_refresh(void)
{
    apple2_t m;
    const char *dir = "test_hostfs_touch_refresh";
    hostfs_volume *vol;
    uint8_t block[512];
    int i;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_touch_refresh/SEED#060000", "s", 1);

    if (!apple2_init(&m)) {
        fail("touch refresh init");
    }
    if (apple2_smartport_mount(&m, 7, 0, dir) != 0) {
        fail("touch refresh mount");
    }
    vol = m.sp_device[7].hostfs[0];
    if (vol == NULL) {
        fail("touch refresh vol");
    }

    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);
    write_file("test_hostfs_touch_refresh/IDLE#060000", "i", 1);

    /* Advance plenty of emulated Φ0; peripherals must not refresh idle HostFS. */
    m.cpu.cpu.cycles = 5000000ull;
    for (i = 0; i < 100; ++i) {
        apple2_peripherals_step(&m, 10000);
    }
    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read dir after idle step");
    }
    if (find_entry_in_dir_block(block, "IDLE", NULL, NULL, NULL)) {
        fail("idle peripherals must not rescan");
    }

    /* A healthy touch with no event performs no readdir and no stat. */
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read after early touch");
    }
    if (find_entry_in_dir_block(block, "IDLE", NULL, NULL, NULL)) {
        fail("touch without an event must not rescan");
    }
    if (hostfs_test_targeted_directory_scans(vol) != 0 ||
        hostfs_test_targeted_file_stats(vol) != 0 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("idle notification mode performed refresh work");
    }

    if (!hostfs_test_inject_event(vol, FS_WATCH_CREATE, "IDLE#060000") ||
        !hostfs_test_inject_event(vol, FS_WATCH_METADATA, "IDLE#060000")) {
        fail("inject root create");
    }
    /* A SmartPort STATUS touch drains both duplicate path invalidations once. */
    m.sp_device[7].sp_buffer[1] = 0;
    sp_status(&m, 7);
    if (m.sp_device[7].sp_buffer[0] != SP_SUCCESS) {
        fail("status after delta");
    }
    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read after event-driven status touch");
    }
    if (!find_entry_in_dir_block(block, "IDLE", NULL, NULL, NULL)) {
        fail("STATUS touch should reconcile queued create");
    }
    if (hostfs_test_targeted_directory_scans(vol) != 1 ||
        hostfs_test_targeted_file_stats(vol) != 0 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("root create should cost one immediate-directory scan");
    }

    write_file("test_hostfs_touch_refresh/NEXT#060000", "n", 1);
    if (!hostfs_test_inject_event(vol, FS_WATCH_CREATE, "NEXT#060000")) {
        fail("inject sealed create");
    }
    m.replay_sealed = true;
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read after second early touch");
    }
    if (find_entry_in_dir_block(block, "NEXT", NULL, NULL, NULL)) {
        fail("sealed replay must defer queued filesystem events");
    }
    m.replay_sealed = false;
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, 2, block) != 0 ||
        !find_entry_in_dir_block(block, "NEXT", NULL, NULL, NULL) ||
        hostfs_test_targeted_directory_scans(vol) != 2) {
        fail("deferred event must reconcile after sealed replay");
    }

    /* The public escape hatch remains an unconditional recursive rescan. */
    write_file("test_hostfs_touch_refresh/FORCE#060000", "f", 1);
    if (hostfs_rescan(vol) != 0) {
        fail("force rescan");
    }
    if (hostfs_read_block(vol, 2, block) != 0) {
        fail("read after force");
    }
    if (!find_entry_in_dir_block(block, "FORCE", NULL, NULL, NULL)) {
        fail("force rescan should see FORCE");
    }
    if (hostfs_test_full_rescans(vol) != 1) {
        fail("explicit hostfs_rescan must count as one full scan");
    }

    if (apple2_smartport_eject(&m, 7, 0) != 0) {
        fail("touch refresh eject");
    }
    apple2_shutdown(&m);
    wipe_tree(dir);
}

/* ---- ProDOS 51-entry volume directory ---- */

/* Walk the whole four-block volume directory (2-5), not just block 2. Counts
   live entries and optionally looks for one by ProDOS name. */
static int root_catalog(hostfs_volume *vol, const char *want, int *out_count)
{
    int found = 0;
    int count = 0;
    uint16_t b;

    for (b = 2u; b < 2u + HOSTFS_ROOT_DIR_BLOCKS; ++b) {
        uint8_t blk[512];
        int slot;
        /* Slot 0 of the first block is the volume header, not a file. */
        int start = (b == 2u) ? 1 : 0;

        if (hostfs_read_block(vol, b, blk) != 0) {
            fail("read volume directory block");
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            const uint8_t *e = blk + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t nl = (uint8_t)(e[0] & 0x0Fu);
            char nm[HOSTFS_NAME_MAX];

            if (e[0] == 0) {
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            count++;
            if (want != NULL && strcmp(nm, want) == 0) {
                found = 1;
            }
        }
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found;
}

/* F00#060000 .. FNN#060000 — two digits so the case-insensitive scan order and
   the manifest order agree, and the 52nd entry is unambiguous. */
static void make_numbered_files(const char *dir, int first, int last)
{
    int i;
    for (i = first; i <= last; ++i) {
        char path[512];
        snprintf(path, sizeof(path), "%s/F%02d#060000", dir, i);
        write_file(path, "x", 1);
    }
}

static void write_numbered_order_file(const char *dir, int first, int last)
{
    char path[512];
    FILE *fp;
    int i;

    snprintf(path, sizeof(path), "%s/%s", dir, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "w");
    if (fp == NULL) {
        fail("write order file");
    }
    fprintf(fp, "# a2m HostFS catalog order (NAPS files and directory basenames)\n");
    for (i = first; i <= last; ++i) {
        fprintf(fp, "F%02d#060000\n", i);
    }
    fclose(fp);
}

static long slurp(const char *path, char *out, size_t out_size)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (fp == NULL) {
        return -1;
    }
    n = fread(out, 1, out_size, fp);
    fclose(fp);
    return (long)n;
}

/* Swap the first two live entries of the volume directory, CAT.DOCTOR-style,
   and write the block back so the guest-side reconcile runs. */
static void swap_first_two_root_entries(hostfs_volume *vol)
{
    uint8_t dirblk[512];
    uint8_t tmp[HOSTFS_ENTRY_LENGTH];
    uint8_t *first = NULL;
    uint8_t *second = NULL;
    int slot;

    if (hostfs_read_block(vol, 2, dirblk) != 0) {
        fail("read volume directory for swap");
    }
    for (slot = 1; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
        uint8_t *e = dirblk + 4 + slot * HOSTFS_ENTRY_LENGTH;
        if (e[0] == 0) {
            continue;
        }
        if (first == NULL) {
            first = e;
        } else {
            second = e;
            break;
        }
    }
    if (first == NULL || second == NULL) {
        fail("need two root entries to swap");
    }
    memcpy(tmp, first, HOSTFS_ENTRY_LENGTH);
    memcpy(first, second, HOSTFS_ENTRY_LENGTH);
    memcpy(second, tmp, HOSTFS_ENTRY_LENGTH);
    if (hostfs_write_block(vol, 2, dirblk) != 0) {
        fail("write swapped volume directory");
    }
}

static void test_root_entry_limit(void)
{
    const char *dir = "test_hostfs_root_limit";
    hostfs_volume *vol;
    uint8_t blk[512];
    int count = 0;
    int mount_warnings;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    make_numbered_files(dir, 0, 59);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("root limit mount");
    }
    mount_warnings = hostfs_test_using_periodic_refresh(vol) ? 1 : 0;

    /* One accumulated line for all nine drops, not one line each. */
    if (hostfs_warning_count(vol) != mount_warnings + 1) {
        fprintf(stderr, "warn_count=%d last=%s\n",
                hostfs_warning_count(vol), hostfs_last_warning(vol));
        fail("expected exactly one root-overflow warning");
    }
    if (strstr(hostfs_last_warning(vol), "dropped 9") == NULL) {
        fprintf(stderr, "warning: %s\n", hostfs_last_warning(vol));
        fail("warning should name the drop count");
    }
    /* Host basenames, not ProDOS names: a dropped entry is only findable on the
       host, so "F51" alone would name a string that exists nowhere. */
    if (strstr(hostfs_last_warning(vol), "F51#060000") == NULL) {
        fprintf(stderr, "warning: %s\n", hostfs_last_warning(vol));
        fail("warning should name dropped host basenames");
    }

    (void)root_catalog(vol, NULL, &count);
    if (count != HOSTFS_ROOT_MAX_ENTRIES) {
        fprintf(stderr, "root entries=%d\n", count);
        fail("volume directory must hold exactly 51 entries");
    }
    if (hostfs_file_count(vol) != HOSTFS_ROOT_MAX_ENTRIES) {
        fprintf(stderr, "file_count=%d\n", hostfs_file_count(vol));
        fail("file_count must match the catalog");
    }
    if (!root_catalog(vol, "F50", NULL)) {
        fail("F50 is the 51st entry and must be present");
    }
    if (root_catalog(vol, "F51", NULL)) {
        fail("F51 is the 52nd entry and must be dropped");
    }

    /* The chain is exactly blocks 2-5, closed at both ends. */
    if (hostfs_read_block(vol, 2, blk) != 0) {
        fail("read block 2");
    }
    if (blk[0] != 0 || blk[1] != 0) {
        fail("block 2 prev must be 0");
    }
    if (blk[2] != 3 || blk[3] != 0) {
        fail("block 2 next must be 3");
    }
    if (hostfs_read_block(vol, 5, blk) != 0) {
        fail("read block 5");
    }
    if (blk[0] != 4 || blk[1] != 0) {
        fail("block 5 prev must be 4");
    }
    if (blk[2] != 0 || blk[3] != 0) {
        fail("block 5 next must be 0");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

/* Over the limit at mount: the catalog is a 51-entry view of a 60-entry folder,
   so a guest-side reorder must not rewrite the user's manifest with the 51. */
static void test_order_manifest_frozen_when_truncated(void)
{
    const char *dir = "test_hostfs_order_freeze";
    char path[512];
    char before[8192];
    char after[8192];
    long n_before;
    long n_after;
    hostfs_volume *vol;
    int mount_warnings;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    make_numbered_files(dir, 0, 59);
    write_numbered_order_file(dir, 0, 59);
    snprintf(path, sizeof(path), "%s/%s", dir, HOSTFS_ORDER_FILENAME);
    n_before = slurp(path, before, sizeof(before));
    if (n_before <= 0) {
        fail("seed order file");
    }

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("freeze mount");
    }
    mount_warnings = hostfs_test_using_periodic_refresh(vol) ? 1 : 0;
    if (hostfs_warning_count(vol) != mount_warnings + 1) {
        fail("expected the root-overflow warning at mount");
    }

    swap_first_two_root_entries(vol);

    /* The freeze fired, rather than the "nothing changed" early return. */
    if (hostfs_warning_count(vol) != mount_warnings + 2 ||
        strstr(hostfs_last_warning(vol), "left unchanged") == NULL) {
        fprintf(stderr, "warn_count=%d last=%s\n",
                hostfs_warning_count(vol), hostfs_last_warning(vol));
        fail("expected the manifest-frozen warning after a root reorder");
    }

    hostfs_eject(vol);

    n_after = slurp(path, after, sizeof(after));
    if (n_after != n_before || memcmp(before, after, (size_t)n_before) != 0) {
        fail("hostfs.order rewritten while the root was truncated");
    }
    wipe_tree(dir);
}

/* Under the limit at mount, filled past it while mounted. hostfs_rescan never
   re-runs the mount scan, so this reaches the freeze through the add path. */
static void test_order_manifest_frozen_after_mount(void)
{
    const char *dir = "test_hostfs_order_fill";
    char path[512];
    char before[8192];
    char after[8192];
    long n_before;
    long n_after;
    hostfs_volume *vol;
    int count = 0;
    int mount_warnings;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    make_numbered_files(dir, 0, 44);
    write_numbered_order_file(dir, 0, 59);
    snprintf(path, sizeof(path), "%s/%s", dir, HOSTFS_ORDER_FILENAME);
    n_before = slurp(path, before, sizeof(before));
    if (n_before <= 0) {
        fail("seed order file");
    }

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("fill mount");
    }
    mount_warnings = hostfs_test_using_periodic_refresh(vol) ? 1 : 0;
    if (hostfs_warning_count(vol) != mount_warnings) {
        fprintf(stderr, "last=%s\n", hostfs_last_warning(vol));
        fail("a root under the limit must mount clean");
    }

    make_numbered_files(dir, 45, 59);
    if (hostfs_rescan(vol) != 0) {
        fail("rescan after filling the root");
    }

    (void)root_catalog(vol, NULL, &count);
    if (count != HOSTFS_ROOT_MAX_ENTRIES) {
        fprintf(stderr, "root entries=%d\n", count);
        fail("rescan must stop at 51 entries");
    }
    if (hostfs_warning_count(vol) != mount_warnings + 2 ||
        strstr(hostfs_last_warning(vol), "left unchanged") == NULL) {
        fprintf(stderr, "warn_count=%d last=%s\n",
                hostfs_warning_count(vol), hostfs_last_warning(vol));
        fail("expected root-full then manifest-frozen warnings");
    }

    hostfs_eject(vol);

    n_after = slurp(path, after, sizeof(after));
    if (n_after != n_before || memcmp(before, after, (size_t)n_before) != 0) {
        fail("hostfs.order rewritten after the root filled up post-mount");
    }
    wipe_tree(dir);
}

static void test_large_subdirectory_mount(void)
{
    const char *dir = "test_hostfs_large_mount";
    hostfs_volume *vol;
    uint8_t blk[512];
    uint16_t dir_key = 0;
    uint16_t file_key = 0;
    uint8_t st = 0;
    uint8_t type = 0;
    int count = 0;
    int mount_warnings;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_large_mount/BIG");
    make_large_numbered_files("test_hostfs_large_mount/BIG", 'F', 0, 999);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("large subdirectory mount");
    }
    mount_warnings = hostfs_test_using_periodic_refresh(vol) ? 1 : 0;
    if (hostfs_warning_count(vol) != mount_warnings) {
        fprintf(stderr, "warn_count=%d last=%s\n",
                hostfs_warning_count(vol), hostfs_last_warning(vol));
        fail("large legal subdirectory must mount without warning");
    }
    if (hostfs_read_block(vol, 2, blk) != 0 ||
        !find_entry_in_dir_block(blk, "BIG", &st, &dir_key, &type)) {
        fail("BIG missing from root");
    }
    if (!subdir_catalog(vol, dir_key, "F0999", &count, &file_key) || count != 1000) {
        fprintf(stderr, "large catalog count=%d\n", count);
        fail("large subdirectory must enumerate all 1000 entries");
    }
    if (hostfs_read_block(vol, file_key, blk) != 0 ||
        blk[0] != 'F' || blk[1] != 0xE7u || blk[2] != 0x03u) {
        fail("read final file in large subdirectory");
    }
    if (hostfs_file_count(vol) != 1001) {
        fprintf(stderr, "large file_count=%d\n", hostfs_file_count(vol));
        fail("large subdirectory node count");
    }
    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_large_subdirectory_rescan(void)
{
    const char *dir = "test_hostfs_large_rescan";
    hostfs_volume *vol;
    uint8_t blk[HOSTFS_BLOCK_SIZE];
    uint16_t dir_key = 0;
    uint16_t file_key = 0;
    int count = 0;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_large_rescan/BIG");
    make_large_numbered_files("test_hostfs_large_rescan/BIG", 'F', 0, 999);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL || hostfs_read_block(vol, 2, blk) != 0 ||
        !find_entry_in_dir_block(blk, "BIG", NULL, &dir_key, NULL)) {
        fail("large rescan fixture mount");
    }
    if (remove("test_hostfs_large_rescan/BIG/F0500#060000") != 0) {
        fail("remove file before large rescan");
    }
    make_large_numbered_files("test_hostfs_large_rescan/BIG", 'F', 1000, 1000);
    if (hostfs_rescan(vol) != 0) {
        fail("rescan 1000-entry subdirectory");
    }
    if (subdir_catalog(vol, dir_key, "F0500", NULL, NULL)) {
        fail("large rescan must remove missing entry");
    }
    if (!subdir_catalog(vol, dir_key, "F1000", &count, &file_key) || count != 1000) {
        fprintf(stderr, "rescanned catalog count=%d\n", count);
        fail("large rescan must retain 1000 entries");
    }
    if (hostfs_read_block(vol, file_key, blk) != 0 ||
        blk[0] != 'F' || blk[1] != 0xE8u || blk[2] != 0x03u) {
        fail("read file added by large rescan");
    }
    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_block_index_create_delete_churn(void)
{
    const char *dir = "test_hostfs_block_index";
    const char *subdir = "test_hostfs_block_index/CHURN";
    hostfs_volume *vol;
    uint8_t blk[HOSTFS_BLOCK_SIZE];
    uint16_t dir_key = 0;
    uint16_t target_key = 0;
    uint16_t peer_key = 0;
    char current = 'F';
    int round;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR(subdir);
    make_large_numbered_files(subdir, current, 0, 119);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL || hostfs_read_block(vol, 2, blk) != 0 ||
        !find_entry_in_dir_block(blk, "CHURN", NULL, &dir_key, NULL)) {
        fail("block index churn fixture mount");
    }

    for (round = 0; round < 3; ++round) {
        char next = current == 'F' ? 'G' : 'F';
        char final_name[HOSTFS_NAME_MAX];
        remove_large_numbered_files(subdir, current, 0, 119);
        make_large_numbered_files(subdir, next, 0, 119);
        if (hostfs_rescan(vol) != 0) {
            fail("block index churn rescan");
        }
        snprintf(final_name, sizeof(final_name), "%c0119", next);
        if (!subdir_catalog(vol, dir_key, final_name, NULL, &target_key) ||
            hostfs_read_block(vol, target_key, blk) != 0 ||
            blk[0] != (uint8_t)next || blk[1] != 119u) {
            fail("block index drift after create/delete churn");
        }
        current = next;
    }

    /* A wrong direct slot can corrupt writes as well as reads. Write one file,
       then prove a peer still resolves to its own block and payload. */
    memset(blk, 0xC7, sizeof(blk));
    if (hostfs_write_block(vol, target_key, blk) != 0) {
        fail("write through indexed block after churn");
    }
    if (!subdir_catalog(vol, dir_key, "G0042", NULL, &peer_key) ||
        hostfs_read_block(vol, peer_key, blk) != 0 ||
        blk[0] != 'G' || blk[1] != 42u) {
        fail("indexed write must not redirect a peer block");
    }
    if (hostfs_read_block(vol, target_key, blk) != 0 || blk[0] != 0xC7u) {
        fail("indexed write target payload");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_targeted_deep_add_delete(void)
{
    const char *dir = "test_hostfs_targeted_deep";
    hostfs_volume *vol;
    uint8_t block[HOSTFS_BLOCK_SIZE];
    uint16_t left_key = 0;
    uint16_t deep_key = 0;
    uint16_t right_key = 0;
    int right_count = 0;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_targeted_deep/LEFT");
    HOSTFS_MKDIR("test_hostfs_targeted_deep/LEFT/DEEP");
    HOSTFS_MKDIR("test_hostfs_targeted_deep/RIGHT");
    write_file("test_hostfs_targeted_deep/RIGHT/KEEP#060000", "k", 1);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL || hostfs_read_block(vol, 2, block) != 0 ||
        !find_entry_in_dir_block(block, "LEFT", NULL, &left_key, NULL) ||
        !find_entry_in_dir_block(block, "RIGHT", NULL, &right_key, NULL) ||
        !subdir_catalog(vol, left_key, "DEEP", NULL, &deep_key)) {
        fail("targeted deep fixture mount");
    }
    (void)subdir_catalog(vol, right_key, NULL, &right_count, NULL);
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);

    write_file("test_hostfs_targeted_deep/LEFT/DEEP/NEW#060000", "new", 3);
    if (!hostfs_test_inject_event(
            vol, FS_WATCH_CREATE, "LEFT/DEEP/NEW#060000")) {
        fail("inject deep add");
    }
    hostfs_maybe_refresh(vol);
    if (!subdir_catalog(vol, deep_key, "NEW", NULL, NULL) ||
        hostfs_test_targeted_directory_scans(vol) != 1 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("deep add must scan only its immediate parent");
    }
    {
        int count = 0;
        (void)subdir_catalog(vol, right_key, NULL, &count, NULL);
        if (count != right_count) {
            fail("deep add touched sibling catalog");
        }
    }

    if (remove("test_hostfs_targeted_deep/LEFT/DEEP/NEW#060000") != 0 ||
        !hostfs_test_inject_event(
            vol, FS_WATCH_REMOVE, "LEFT/DEEP/NEW#060000")) {
        fail("inject deep delete");
    }
    hostfs_maybe_refresh(vol);
    if (subdir_catalog(vol, deep_key, "NEW", NULL, NULL) ||
        hostfs_test_targeted_directory_scans(vol) != 2 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("deep delete must scan only its immediate parent");
    }

    /* A newly moved-in directory is different: watch it first, then materialize
       the subtree that already exists inside it. Existing DEEP is not descended. */
    HOSTFS_MKDIR("test_hostfs_targeted_deep/LEFT/ADDED");
    HOSTFS_MKDIR("test_hostfs_targeted_deep/LEFT/ADDED/NEST");
    write_file(
        "test_hostfs_targeted_deep/LEFT/ADDED/NEST/LEAF#060000", "leaf", 4);
    if (!hostfs_test_inject_event(
            vol, FS_WATCH_CREATE | FS_WATCH_DIRECTORY, "LEFT/ADDED")) {
        fail("inject moved-in directory");
    }
    hostfs_maybe_refresh(vol);
    {
        uint16_t added_key = 0;
        uint16_t nest_key = 0;
        if (!subdir_catalog(vol, left_key, "ADDED", NULL, &added_key) ||
            !subdir_catalog(vol, added_key, "NEST", NULL, &nest_key) ||
            !subdir_catalog(vol, nest_key, "LEAF", NULL, NULL) ||
            hostfs_test_targeted_directory_scans(vol) != 5 ||
            hostfs_test_full_rescans(vol) != 0) {
            fail("new directory subtree was not recursively materialized");
        }
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_targeted_file_resize(void)
{
    const char *dir = "test_hostfs_targeted_resize";
    hostfs_volume *vol;
    uint8_t payload[700];
    uint8_t root[HOSTFS_BLOCK_SIZE];
    uint8_t index[HOSTFS_BLOCK_SIZE];
    uint8_t data[HOSTFS_BLOCK_SIZE];
    uint8_t storage = 0;
    uint16_t key = 0;
    uint16_t second_data;
    int i;

    for (i = 0; i < (int)sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i ^ 0x5Au);
    }
    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_targeted_resize/DATA#060000", payload, 500);
    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("targeted resize mount");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);

    write_file("test_hostfs_targeted_resize/DATA#060000", payload, sizeof(payload));
    if (!hostfs_test_inject_event(vol, FS_WATCH_MODIFY, "DATA#060000")) {
        fail("inject file resize");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, 2, root) != 0 ||
        !find_entry_in_dir_block(root, "DATA", &storage, &key, NULL) ||
        storage != 2u) {
        fail("resize must update seedling to sapling catalog metadata");
    }
    if (hostfs_read_block(vol, key, index) != 0) {
        fail("read resized sapling index");
    }
    second_data = (uint16_t)(index[1] | ((uint16_t)index[257] << 8));
    if (second_data == 0u || hostfs_read_block(vol, second_data, data) != 0 ||
        memcmp(data, payload + HOSTFS_BLOCK_SIZE,
               sizeof(payload) - HOSTFS_BLOCK_SIZE) != 0) {
        fail("resized sapling second block");
    }
    if (hostfs_test_targeted_file_stats(vol) != 1 ||
        hostfs_test_targeted_directory_scans(vol) != 0 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("known modify must cost one stat and no directory scan");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_targeted_external_move(void)
{
    const char *dir = "test_hostfs_targeted_move";
    hostfs_volume *vol;
    uint8_t root[HOSTFS_BLOCK_SIZE];
    uint8_t data[HOSTFS_BLOCK_SIZE];
    uint16_t src_key = 0;
    uint16_t dst_key = 0;
    uint16_t file_key = 0;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_targeted_move/SRC");
    HOSTFS_MKDIR("test_hostfs_targeted_move/DST");
    write_file("test_hostfs_targeted_move/SRC/OLD#060000", "move", 4);
    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL || hostfs_read_block(vol, 2, root) != 0 ||
        !find_entry_in_dir_block(root, "SRC", NULL, &src_key, NULL) ||
        !find_entry_in_dir_block(root, "DST", NULL, &dst_key, NULL)) {
        fail("targeted move fixture mount");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);
    if (rename("test_hostfs_targeted_move/SRC/OLD#060000",
               "test_hostfs_targeted_move/DST/NEW#060000") != 0 ||
        !hostfs_test_inject_event(vol, FS_WATCH_RENAME, "SRC/OLD#060000") ||
        !hostfs_test_inject_event(vol, FS_WATCH_RENAME, "DST/NEW#060000")) {
        fail("inject external move");
    }
    hostfs_maybe_refresh(vol);
    if (subdir_catalog(vol, src_key, "OLD", NULL, NULL) ||
        !subdir_catalog(vol, dst_key, "NEW", NULL, &file_key) ||
        hostfs_read_block(vol, file_key, data) != 0 ||
        memcmp(data, "move", 4) != 0) {
        fail("external move did not reconcile old and new parents");
    }
    if (hostfs_test_targeted_directory_scans(vol) != 2 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("complete external move must scan exactly two parents");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_targeted_order_edit(void)
{
    const char *dir = "test_hostfs_targeted_order";
    hostfs_volume *vol;
    uint8_t root[HOSTFS_BLOCK_SIZE];
    uint8_t block[HOSTFS_BLOCK_SIZE];
    uint16_t a_key = 0;
    uint16_t b_key = 0;
    char name[HOSTFS_NAME_MAX];
    uint8_t nl;
    const char order[] = "TWO#060000\nONE#060000\n";

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_targeted_order/A");
    HOSTFS_MKDIR("test_hostfs_targeted_order/B");
    write_file("test_hostfs_targeted_order/A/ONE#060000", "1", 1);
    write_file("test_hostfs_targeted_order/A/TWO#060000", "2", 1);
    write_file("test_hostfs_targeted_order/B/ALPHA#060000", "a", 1);
    write_file("test_hostfs_targeted_order/B/BETA#060000", "b", 1);
    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL || hostfs_read_block(vol, 2, root) != 0 ||
        !find_entry_in_dir_block(root, "A", NULL, &a_key, NULL) ||
        !find_entry_in_dir_block(root, "B", NULL, &b_key, NULL)) {
        fail("targeted order fixture mount");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);
    write_file("test_hostfs_targeted_order/A/hostfs.order", order, sizeof(order) - 1u);
    if (!hostfs_test_inject_event(vol, FS_WATCH_MODIFY, "A/hostfs.order")) {
        fail("inject order edit");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, a_key, block) != 0) {
        fail("read reordered A");
    }
    nl = (uint8_t)(block[4 + HOSTFS_ENTRY_LENGTH] & 0x0Fu);
    memcpy(name, block + 5 + HOSTFS_ENTRY_LENGTH, nl);
    name[nl] = '\0';
    if (strcmp(name, "TWO") != 0) {
        fail("hostfs.order edit did not reorder its directory");
    }
    if (hostfs_read_block(vol, b_key, block) != 0) {
        fail("read untouched B");
    }
    nl = (uint8_t)(block[4 + HOSTFS_ENTRY_LENGTH] & 0x0Fu);
    memcpy(name, block + 5 + HOSTFS_ENTRY_LENGTH, nl);
    name[nl] = '\0';
    if (strcmp(name, "ALPHA") != 0 ||
        hostfs_test_targeted_directory_scans(vol) != 1 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("order edit touched an unrelated directory");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

static void test_event_loss_full_rescan(void)
{
    const char *dir = "test_hostfs_event_loss";
    hostfs_volume *vol;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    write_file("test_hostfs_event_loss/SEED#060000", "s", 1);
    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("event loss mount");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);
    write_file("test_hostfs_event_loss/FOUND#060000", "f", 1);
    hostfs_test_require_full_rescan(vol);
    hostfs_maybe_refresh(vol);
    if (!root_catalog(vol, "FOUND", NULL) ||
        hostfs_test_full_rescans(vol) != 1 ||
        hostfs_test_targeted_directory_scans(vol) != 0) {
        fail("event loss must widen once to a full authoritative rescan");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_test_full_rescans(vol) != 1) {
        fail("taken loss state triggered a second full rescan");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}

#if !defined(_WIN32)
/* A guest-side cross-directory move whose host rename fails must not cost the
   user the file. The node's parent has to follow the catalog even so: leaving
   it behind makes the old parent's next reconcile see an entry that vanished
   from its own catalog and unlink the real host file. */
static void test_failed_move_keeps_host_file(void)
{
    const char *dir = "test_hostfs_failed_move";
    hostfs_volume *vol;
    uint8_t rootblk[512];
    uint8_t srcblk[512];
    uint8_t dstblk[512];
    uint8_t *entry;
    uint8_t *slot = NULL;
    uint16_t src_key = 0;
    uint16_t dst_key = 0;
    uint8_t stype = 0;
    uint8_t ftype = 0;
    struct stat st;
    int i;

    wipe_tree(dir);
    HOSTFS_MKDIR(dir);
    HOSTFS_MKDIR("test_hostfs_failed_move/SRC");
    HOSTFS_MKDIR("test_hostfs_failed_move/DST");
    write_file("test_hostfs_failed_move/SRC/DATA#060000", "precious", 8);

    vol = hostfs_mount(dir, "HOSTFS.S7D0");
    if (vol == NULL) {
        fail("failed move mount");
    }
    hostfs_test_use_synthetic_events(vol);
    hostfs_test_reset_refresh_counters(vol);
    if (hostfs_read_block(vol, 2, rootblk) != 0 ||
        !find_entry_in_dir_block(rootblk, "SRC", &stype, &src_key, &ftype) ||
        !find_entry_in_dir_block(rootblk, "DST", &stype, &dst_key, &ftype)) {
        fail("SRC and DST must be in the catalog");
    }

    /* Copy DATA's entry from SRC's block into DST's, as a guest move would. */
    if (hostfs_read_block(vol, src_key, srcblk) != 0 ||
        hostfs_read_block(vol, dst_key, dstblk) != 0) {
        fail("read subdirectory blocks");
    }
    entry = NULL;
    for (i = 1; i < HOSTFS_ENTRIES_PER_BLOCK; ++i) {
        uint8_t *e = srcblk + 4 + i * HOSTFS_ENTRY_LENGTH;
        uint8_t nl = (uint8_t)(e[0] & 0x0Fu);
        char nm[HOSTFS_NAME_MAX];
        if (e[0] == 0) {
            continue;
        }
        memcpy(nm, e + 1, nl);
        nm[nl] = '\0';
        if (strcmp(nm, "DATA") == 0) {
            entry = e;
            break;
        }
    }
    if (entry == NULL) {
        fail("DATA missing from SRC");
    }
    for (i = 1; i < HOSTFS_ENTRIES_PER_BLOCK; ++i) {
        slot = dstblk + 4 + i * HOSTFS_ENTRY_LENGTH;
        if (slot[0] == 0) {
            break;
        }
    }
    memcpy(slot, entry, HOSTFS_ENTRY_LENGTH);
    slot[0x25] = (uint8_t)(dst_key & 0xFFu);
    slot[0x26] = (uint8_t)((dst_key >> 8) & 0xFFu);
    dstblk[4 + 0x21] = 1;
    memset(entry, 0, HOSTFS_ENTRY_LENGTH);
    srcblk[4 + 0x21] = 0;

    /* Make the host rename fail: DST is not writable. */
    if (chmod("test_hostfs_failed_move/DST", 0500) != 0) {
        fail("chmod DST");
    }
    if (hostfs_write_block(vol, dst_key, dstblk) != 0) {
        fail("publish DST entry");
    }
    if (hostfs_warning_count(vol) < 1 ||
        strstr(hostfs_last_warning(vol), "move failed") == NULL) {
        fprintf(stderr, "warn_count=%d last=%s\n",
                hostfs_warning_count(vol), hostfs_last_warning(vol));
        fail("a failed host move must warn");
    }
    /* The old parent's reconcile is what used to delete the file. */
    if (hostfs_write_block(vol, src_key, srcblk) != 0) {
        fail("publish SRC removal");
    }
    if (stat("test_hostfs_failed_move/SRC/DATA#060000", &st) != 0) {
        fail("failed move must not destroy the host file");
    }

    /* With the obstruction gone, the deferred parent invalidations converge
       on the next safe touch without an explicit full rescan. */
    if (chmod("test_hostfs_failed_move/DST", 0700) != 0) {
        fail("restore DST mode");
    }
    hostfs_maybe_refresh(vol);
    if (hostfs_read_block(vol, dst_key, dstblk) != 0 ||
        find_entry_in_dir_block(dstblk, "DATA", &stype, &src_key, &ftype)) {
        fail("phantom DATA should be gone from DST");
    }
    if (stat("test_hostfs_failed_move/SRC/DATA#060000", &st) != 0) {
        fail("host file must survive the rescan");
    }
    if (hostfs_test_targeted_directory_scans(vol) != 2 ||
        hostfs_test_full_rescans(vol) != 0) {
        fail("failed move recovery must scan only SRC and DST");
    }

    hostfs_eject(vol);
    wipe_tree(dir);
}
#endif

int main(void)
{
    test_naps_and_mangle();
    test_volume_map();
    test_nested_directories();
    test_nested_rescan();
    test_smartport_hostfs_and_mixed();
    test_write_through_and_rescan();
    test_create_reconcile();
    test_dir_write_through();
    test_order_manifest();
    test_touch_refresh();
    test_root_entry_limit();
    test_order_manifest_frozen_when_truncated();
    test_order_manifest_frozen_after_mount();
    test_large_subdirectory_mount();
    test_large_subdirectory_rescan();
    test_block_index_create_delete_churn();
    test_targeted_deep_add_delete();
    test_targeted_file_resize();
    test_targeted_external_move();
    test_targeted_order_edit();
    test_event_loss_full_rescan();
#if !defined(_WIN32)
    test_failed_move_keeps_host_file();
#endif
    printf("hostfs tests ok\n");
    return 0;
}

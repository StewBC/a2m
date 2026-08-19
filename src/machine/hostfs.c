/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#include "hostfs.h"
#include "hostfs_boot.h"

#include "apple2_file.h"
#include "a2_status.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#define A2M_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#else
#include <dirent.h>
#include <unistd.h>
#define A2M_STAT_ISDIR(mode) S_ISDIR(mode)
#endif

enum {
    HOSTFS_ENTRY_LENGTH = 39,
    HOSTFS_ENTRIES_PER_BLOCK = 13,
    HOSTFS_BITMAP_BLOCKS = 16, /* ceil(65535 / 4096) */
    HOSTFS_ACCESS_LOCKED = 0x21u, /* read + backup */
    HOSTFS_ACCESS_VOL = 0xC3u
};

typedef enum {
    HOSTFS_MAP_RAM = 1,
    HOSTFS_MAP_HOST = 2
} hostfs_map_kind;

typedef struct {
    uint16_t block;
    hostfs_map_kind kind;
    union {
        uint8_t *ram;
        struct {
            int file_index;
            uint32_t offset;
        } host;
    } u;
} hostfs_block_ref;

typedef struct {
    char prodos_name[HOSTFS_NAME_MAX];
    uint8_t name_len;
    uint8_t file_type;
    uint16_t aux_type;
    uint32_t eof;
    uint8_t storage_type; /* 1 seedling, 2 sapling, 3 tree */
    uint16_t key_block;
    uint16_t blocks_used;
    char host_path[HOSTFS_PATH_MAX];
} hostfs_file;

struct hostfs_volume {
    char root_path[HOSTFS_PATH_MAX];
    char volume_name[HOSTFS_NAME_MAX];
    uint8_t volume_name_len;
    uint16_t total_blocks;
    uint16_t bitmap_block;
    uint16_t dir_block_count;
    uint16_t next_block;

    hostfs_file files[HOSTFS_MAX_FILES];
    int file_count;

    hostfs_block_ref *map;
    int map_count;
    int map_cap;

    uint8_t *bitmap; /* HOSTFS_BITMAP_BLOCKS * 512 */
};

bool hostfs_path_is_dir(const char *path)
{
    struct stat st;

    return path != NULL && path[0] != '\0' &&
           stat(path, &st) == 0 && A2M_STAT_ISDIR(st.st_mode);
}

uint16_t hostfs_total_blocks(const hostfs_volume *vol)
{
    return vol != NULL ? vol->total_blocks : 0u;
}

const char *hostfs_root_path(const hostfs_volume *vol)
{
    return vol != NULL ? vol->root_path : NULL;
}

const char *hostfs_volume_name(const hostfs_volume *vol)
{
    return vol != NULL ? vol->volume_name : NULL;
}

int hostfs_file_count(const hostfs_volume *vol)
{
    return vol != NULL ? vol->file_count : 0;
}

bool hostfs_mangle_prodos_name(const char *stem, char *out, size_t out_size)
{
    size_t i;
    size_t o = 0;

    if (stem == NULL || out == NULL || out_size < 2u) {
        return false;
    }
    for (i = 0; stem[i] != '\0' && o < 15u; ++i) {
        unsigned char c = (unsigned char)stem[i];
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.') {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
    if (o == 0u) {
        return false;
    }
    /* ProDOS names must start with a letter. */
    if (!(out[0] >= 'A' && out[0] <= 'Z')) {
        return false;
    }
    return true;
}

bool hostfs_naps_parse_name(
    const char *filename,
    char *prodos_name,
    size_t prodos_name_size,
    uint8_t *file_type,
    uint16_t *aux_type)
{
    size_t len;
    char stem[256];
    uint8_t type = 0;
    uint16_t aux = 0;

    if (filename == NULL || filename[0] == '\0') {
        return false;
    }
    if (!apple2_naps_parse_path(filename, &type, &aux)) {
        return false;
    }
    len = strlen(filename);
    if (len < 7u) {
        return false;
    }
    len -= 7u;
    if (len >= sizeof(stem)) {
        len = sizeof(stem) - 1u;
    }
    memcpy(stem, filename, len);
    stem[len] = '\0';
    if (!hostfs_mangle_prodos_name(stem, prodos_name, prodos_name_size)) {
        return false;
    }
    if (file_type != NULL) {
        *file_type = type;
    }
    if (aux_type != NULL) {
        *aux_type = aux;
    }
    return true;
}

static void hostfs_path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    size_t dir_len;

    if (out == NULL || out_size == 0) {
        return;
    }
    if (dir == NULL || dir[0] == '\0') {
        snprintf(out, out_size, "%s", name != NULL ? name : "");
        return;
    }
    dir_len = strlen(dir);
    if (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
        snprintf(out, out_size, "%s%s", dir, name != NULL ? name : "");
    } else {
#if defined(_WIN32)
        snprintf(out, out_size, "%s\\%s", dir, name != NULL ? name : "");
#else
        snprintf(out, out_size, "%s/%s", dir, name != NULL ? name : "");
#endif
    }
}

static int hostfs_map_find(const hostfs_volume *vol, uint16_t block)
{
    int i;
    for (i = 0; i < vol->map_count; ++i) {
        if (vol->map[i].block == block) {
            return i;
        }
    }
    return -1;
}

static int hostfs_map_add_ram(hostfs_volume *vol, uint16_t block, const uint8_t *data)
{
    hostfs_block_ref *ref;
    uint8_t *ram;

    if (hostfs_map_find(vol, block) >= 0) {
        return A2_ERR;
    }
    if (vol->map_count >= vol->map_cap) {
        int new_cap = vol->map_cap == 0 ? 64 : vol->map_cap * 2;
        hostfs_block_ref *n = (hostfs_block_ref *)realloc(
            vol->map, (size_t)new_cap * sizeof(hostfs_block_ref));
        if (n == NULL) {
            return A2_ERR;
        }
        vol->map = n;
        vol->map_cap = new_cap;
    }
    ram = (uint8_t *)malloc(HOSTFS_BLOCK_SIZE);
    if (ram == NULL) {
        return A2_ERR;
    }
    if (data != NULL) {
        memcpy(ram, data, HOSTFS_BLOCK_SIZE);
    } else {
        memset(ram, 0, HOSTFS_BLOCK_SIZE);
    }
    ref = &vol->map[vol->map_count++];
    ref->block = block;
    ref->kind = HOSTFS_MAP_RAM;
    ref->u.ram = ram;
    return A2_OK;
}

static int hostfs_map_add_host(
    hostfs_volume *vol, uint16_t block, int file_index, uint32_t offset)
{
    hostfs_block_ref *ref;

    if (hostfs_map_find(vol, block) >= 0) {
        return A2_ERR;
    }
    if (vol->map_count >= vol->map_cap) {
        int new_cap = vol->map_cap == 0 ? 64 : vol->map_cap * 2;
        hostfs_block_ref *n = (hostfs_block_ref *)realloc(
            vol->map, (size_t)new_cap * sizeof(hostfs_block_ref));
        if (n == NULL) {
            return A2_ERR;
        }
        vol->map = n;
        vol->map_cap = new_cap;
    }
    ref = &vol->map[vol->map_count++];
    ref->block = block;
    ref->kind = HOSTFS_MAP_HOST;
    ref->u.host.file_index = file_index;
    ref->u.host.offset = offset;
    return A2_OK;
}

static uint8_t *hostfs_map_ram_ptr(hostfs_volume *vol, uint16_t block)
{
    int idx = hostfs_map_find(vol, block);
    if (idx < 0 || vol->map[idx].kind != HOSTFS_MAP_RAM) {
        return NULL;
    }
    return vol->map[idx].u.ram;
}

static uint16_t hostfs_alloc_block(hostfs_volume *vol)
{
    uint16_t b = vol->next_block;
    if (b >= vol->total_blocks) {
        return 0;
    }
    vol->next_block = (uint16_t)(b + 1u);
    return b;
}

/* ProDOS volume bitmap: 1 = free, 0 = used. Bit 7 of byte 0 is block 0. */
static void hostfs_bitmap_mark_used(uint8_t *bitmap, uint16_t block)
{
    size_t byte_index = (size_t)block / 8u;
    unsigned bit = 7u - ((unsigned)block % 8u);
    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] & (uint8_t)~(1u << bit));
}

static void hostfs_index_set(uint8_t *index, unsigned slot, uint16_t block)
{
    index[slot] = (uint8_t)(block & 0xFFu);
    index[256u + slot] = (uint8_t)((block >> 8) & 0xFFu);
}

static void hostfs_write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void hostfs_write_u24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
}

static int hostfs_name_exists(const hostfs_volume *vol, const char *name)
{
    int i;
    for (i = 0; i < vol->file_count; ++i) {
        if (strcmp(vol->files[i].prodos_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hostfs_scan_directory(hostfs_volume *vol)
{
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search[HOSTFS_PATH_MAX];

    hostfs_path_join(search, sizeof(search), vol->root_path, "*");
    handle = FindFirstFileA(search, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return A2_ERR;
    }
    do {
        char prodos[HOSTFS_NAME_MAX];
        uint8_t type = 0;
        uint16_t aux = 0;
        char full[HOSTFS_PATH_MAX];
        struct stat st;
        hostfs_file *file;

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (!hostfs_naps_parse_name(data.cFileName, prodos, sizeof(prodos), &type, &aux)) {
            continue;
        }
        if (hostfs_name_exists(vol, prodos)) {
            continue;
        }
        if (vol->file_count >= HOSTFS_MAX_FILES) {
            break;
        }
        hostfs_path_join(full, sizeof(full), vol->root_path, data.cFileName);
        if (stat(full, &st) != 0 || A2M_STAT_ISDIR(st.st_mode)) {
            continue;
        }
        file = &vol->files[vol->file_count++];
        memset(file, 0, sizeof(*file));
        snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", prodos);
        file->name_len = (uint8_t)strlen(prodos);
        file->file_type = type;
        file->aux_type = aux;
        file->eof = st.st_size < 0 ? 0u : (uint32_t)st.st_size;
        /* ProDOS EOF is 24-bit. */
        if (file->eof > 0x00FFFFFFu) {
            file->eof = 0x00FFFFFFu;
        }
        snprintf(file->host_path, sizeof(file->host_path), "%s", full);
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return A2_OK;
#else
    DIR *dir;
    struct dirent *entry;

    dir = opendir(vol->root_path);
    if (dir == NULL) {
        return A2_ERR;
    }
    while ((entry = readdir(dir)) != NULL) {
        char prodos[HOSTFS_NAME_MAX];
        uint8_t type = 0;
        uint16_t aux = 0;
        char full[HOSTFS_PATH_MAX];
        struct stat st;
        hostfs_file *file;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        hostfs_path_join(full, sizeof(full), vol->root_path, entry->d_name);
        if (stat(full, &st) != 0 || A2M_STAT_ISDIR(st.st_mode)) {
            continue;
        }
        if (!hostfs_naps_parse_name(entry->d_name, prodos, sizeof(prodos), &type, &aux)) {
            continue;
        }
        if (hostfs_name_exists(vol, prodos)) {
            continue;
        }
        if (vol->file_count >= HOSTFS_MAX_FILES) {
            break;
        }
        file = &vol->files[vol->file_count++];
        memset(file, 0, sizeof(*file));
        snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", prodos);
        file->name_len = (uint8_t)strlen(prodos);
        file->file_type = type;
        file->aux_type = aux;
        file->eof = st.st_size < 0 ? 0u : (uint32_t)st.st_size;
        if (file->eof > 0x00FFFFFFu) {
            file->eof = 0x00FFFFFFu;
        }
        snprintf(file->host_path, sizeof(file->host_path), "%s", full);
    }
    closedir(dir);
    return A2_OK;
#endif
}

static uint16_t hostfs_dir_blocks_for_files(int file_count)
{
    if (file_count <= 12) {
        return 1u;
    }
    return (uint16_t)(1u + ((uint16_t)(file_count - 12) + 12u) / 13u);
}

static int hostfs_alloc_file_storage(hostfs_volume *vol, hostfs_file *file, int file_index)
{
    uint32_t data_blocks;
    uint16_t blocks_used = 0;
    uint32_t i;

    data_blocks = (file->eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    if (file->eof == 0u) {
        data_blocks = 1u; /* empty file still needs one key/data block */
    }

    if (data_blocks <= 1u) {
        uint16_t key = hostfs_alloc_block(vol);
        /* Block 0 is reserved; a zero return means the volume is exhausted. */
        if (key == 0u || key >= vol->total_blocks) {
            return A2_ERR;
        }
        file->storage_type = 1u;
        file->key_block = key;
        if (hostfs_map_add_host(vol, key, file_index, 0u) != A2_OK) {
            return A2_ERR;
        }
        blocks_used = 1u;
    } else if (data_blocks <= 256u) {
        uint16_t key = hostfs_alloc_block(vol);
        uint8_t *index;
        if (key == 0u || key >= vol->total_blocks) {
            return A2_ERR;
        }
        if (hostfs_map_add_ram(vol, key, NULL) != A2_OK) {
            return A2_ERR;
        }
        index = hostfs_map_ram_ptr(vol, key);
        file->storage_type = 2u;
        file->key_block = key;
        blocks_used = 1u;
        for (i = 0; i < data_blocks; ++i) {
            uint16_t db = hostfs_alloc_block(vol);
            if (db == 0u || db >= vol->total_blocks) {
                return A2_ERR;
            }
            hostfs_index_set(index, (unsigned)i, db);
            if (hostfs_map_add_host(
                    vol, db, file_index, (uint32_t)(i * HOSTFS_BLOCK_SIZE)) != A2_OK) {
                return A2_ERR;
            }
            blocks_used++;
        }
    } else {
        /* Tree: master index → up to 128 sapling indexes → 256 data each. */
        uint16_t master = hostfs_alloc_block(vol);
        uint8_t *master_idx;
        uint32_t remaining;
        unsigned idx_slot = 0;

        if (data_blocks > 128u * 256u) {
            return A2_ERR;
        }
        if (master == 0u || master >= vol->total_blocks) {
            return A2_ERR;
        }
        if (hostfs_map_add_ram(vol, master, NULL) != A2_OK) {
            return A2_ERR;
        }
        master_idx = hostfs_map_ram_ptr(vol, master);
        file->storage_type = 3u;
        file->key_block = master;
        blocks_used = 1u;
        remaining = data_blocks;
        while (remaining > 0u) {
            uint32_t chunk = remaining > 256u ? 256u : remaining;
            uint16_t ib = hostfs_alloc_block(vol);
            uint8_t *index;
            uint32_t j;

            if (ib == 0u || ib >= vol->total_blocks || idx_slot >= 128u) {
                return A2_ERR;
            }
            if (hostfs_map_add_ram(vol, ib, NULL) != A2_OK) {
                return A2_ERR;
            }
            index = hostfs_map_ram_ptr(vol, ib);
            hostfs_index_set(master_idx, idx_slot, ib);
            idx_slot++;
            blocks_used++;
            for (j = 0; j < chunk; ++j) {
                uint16_t db = hostfs_alloc_block(vol);
                uint32_t logical = (data_blocks - remaining) + j;
                if (db == 0u || db >= vol->total_blocks) {
                    return A2_ERR;
                }
                hostfs_index_set(index, (unsigned)j, db);
                if (hostfs_map_add_host(
                        vol,
                        db,
                        file_index,
                        (uint32_t)(logical * HOSTFS_BLOCK_SIZE)) != A2_OK) {
                    return A2_ERR;
                }
                blocks_used++;
            }
            remaining -= chunk;
        }
    }

    file->blocks_used = blocks_used;
    return A2_OK;
}

static void hostfs_fill_file_entry(uint8_t *entry, const hostfs_file *file, uint16_t header_ptr)
{
    memset(entry, 0, HOSTFS_ENTRY_LENGTH);
    entry[0] = (uint8_t)((file->storage_type << 4) | (file->name_len & 0x0Fu));
    memcpy(entry + 1, file->prodos_name, file->name_len);
    entry[0x10] = file->file_type;
    hostfs_write_u16(entry + 0x11, file->key_block);
    hostfs_write_u16(entry + 0x13, file->blocks_used);
    hostfs_write_u24(entry + 0x15, file->eof);
    entry[0x1C] = 0; /* version */
    entry[0x1D] = 0; /* min_version */
    entry[0x1E] = HOSTFS_ACCESS_LOCKED;
    hostfs_write_u16(entry + 0x1F, file->aux_type);
    hostfs_write_u16(entry + 0x25, header_ptr);
}

static int hostfs_build_directory(hostfs_volume *vol)
{
    uint16_t d;
    int file_index = 0;

    for (d = 0; d < vol->dir_block_count; ++d) {
        uint16_t block = (uint16_t)(2u + d);
        uint8_t *dir;
        uint16_t prev = d == 0u ? 0u : (uint16_t)(block - 1u);
        uint16_t next =
            (d + 1u < vol->dir_block_count) ? (uint16_t)(block + 1u) : 0u;
        int slot;

        if (hostfs_map_add_ram(vol, block, NULL) != A2_OK) {
            return A2_ERR;
        }
        dir = hostfs_map_ram_ptr(vol, block);
        hostfs_write_u16(dir + 0, prev);
        hostfs_write_u16(dir + 2, next);

        if (d == 0u) {
            uint8_t *hdr = dir + 4;
            hdr[0] = (uint8_t)(0xF0u | (vol->volume_name_len & 0x0Fu));
            memcpy(hdr + 1, vol->volume_name, vol->volume_name_len);
            hdr[0x1C] = 0;
            hdr[0x1D] = 0;
            hdr[0x1E] = HOSTFS_ACCESS_VOL;
            hdr[0x1F] = HOSTFS_ENTRY_LENGTH;
            hdr[0x20] = HOSTFS_ENTRIES_PER_BLOCK;
            hostfs_write_u16(hdr + 0x21, (uint16_t)vol->file_count);
            hostfs_write_u16(hdr + 0x23, vol->bitmap_block);
            hostfs_write_u16(hdr + 0x25, vol->total_blocks);
            for (slot = 1; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
                if (file_index >= vol->file_count) {
                    break;
                }
                hostfs_fill_file_entry(
                    dir + 4 + slot * HOSTFS_ENTRY_LENGTH,
                    &vol->files[file_index],
                    2u);
                file_index++;
            }
        } else {
            for (slot = 0; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
                if (file_index >= vol->file_count) {
                    break;
                }
                hostfs_fill_file_entry(
                    dir + 4 + slot * HOSTFS_ENTRY_LENGTH,
                    &vol->files[file_index],
                    2u);
                file_index++;
            }
        }
    }
    return A2_OK;
}

static int hostfs_build_bitmap(hostfs_volume *vol)
{
    uint16_t b;
    int i;
    size_t bitmap_bytes = (size_t)HOSTFS_BITMAP_BLOCKS * HOSTFS_BLOCK_SIZE;

    vol->bitmap = (uint8_t *)malloc(bitmap_bytes);
    if (vol->bitmap == NULL) {
        return A2_ERR;
    }
    /* All free. */
    memset(vol->bitmap, 0xFF, bitmap_bytes);
    /* Blocks beyond total_blocks stay marked free or unused; mark only used. */

    for (b = 0; b < vol->next_block; ++b) {
        hostfs_bitmap_mark_used(vol->bitmap, b);
    }
    /* Also mark any mapped blocks (should already be < next_block). */
    for (i = 0; i < vol->map_count; ++i) {
        hostfs_bitmap_mark_used(vol->bitmap, vol->map[i].block);
    }

    for (b = 0; b < HOSTFS_BITMAP_BLOCKS; ++b) {
        uint16_t block = (uint16_t)(vol->bitmap_block + b);
        if (hostfs_map_add_ram(
                vol, block, vol->bitmap + (size_t)b * HOSTFS_BLOCK_SIZE) != A2_OK) {
            return A2_ERR;
        }
        /* Bitmap blocks themselves were allocated before file data; already marked. */
        (void)block;
    }
    return A2_OK;
}

void hostfs_eject(hostfs_volume *vol)
{
    int i;
    if (vol == NULL) {
        return;
    }
    for (i = 0; i < vol->map_count; ++i) {
        if (vol->map[i].kind == HOSTFS_MAP_RAM) {
            free(vol->map[i].u.ram);
            vol->map[i].u.ram = NULL;
        }
    }
    free(vol->map);
    free(vol->bitmap);
    free(vol);
}

hostfs_volume *hostfs_mount(const char *root_path, const char *volume_name)
{
    hostfs_volume *vol;
    uint16_t b;
    int i;

    if (root_path == NULL || root_path[0] == '\0' || !hostfs_path_is_dir(root_path)) {
        return NULL;
    }
    if (volume_name == NULL || volume_name[0] == '\0') {
        volume_name = "HOSTFS";
    }

    vol = (hostfs_volume *)calloc(1, sizeof(*vol));
    if (vol == NULL) {
        return NULL;
    }
    snprintf(vol->root_path, sizeof(vol->root_path), "%s", root_path);
    if (!hostfs_mangle_prodos_name(volume_name, vol->volume_name, sizeof(vol->volume_name))) {
        snprintf(vol->volume_name, sizeof(vol->volume_name), "HOSTFS");
    }
    vol->volume_name_len = (uint8_t)strlen(vol->volume_name);
    vol->total_blocks = (uint16_t)HOSTFS_TOTAL_BLOCKS;

    if (hostfs_scan_directory(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }

    vol->dir_block_count = hostfs_dir_blocks_for_files(vol->file_count);
    vol->bitmap_block = (uint16_t)(2u + vol->dir_block_count);

    /* Reserve boot, SOS, directory, bitmap. */
    vol->next_block = 0;
    (void)hostfs_alloc_block(vol); /* 0 boot */
    (void)hostfs_alloc_block(vol); /* 1 */
    for (b = 0; b < vol->dir_block_count; ++b) {
        (void)hostfs_alloc_block(vol);
    }
    for (b = 0; b < HOSTFS_BITMAP_BLOCKS; ++b) {
        (void)hostfs_alloc_block(vol);
    }

    if (hostfs_map_add_ram(vol, 0, hostfs_boot_block0) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }
    if (hostfs_map_add_ram(vol, 1, NULL) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }

    /* Allocate file storage before directory so key_block/blocks_used are known. */
    for (i = 0; i < vol->file_count; ++i) {
        if (hostfs_alloc_file_storage(vol, &vol->files[i], i) != A2_OK) {
            hostfs_eject(vol);
            return NULL;
        }
    }

    if (hostfs_build_directory(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }
    if (hostfs_build_bitmap(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }

    return vol;
}

int hostfs_read_block(hostfs_volume *vol, uint32_t block, uint8_t *out)
{
    int idx;
    FILE *fp;
    size_t got;

    if (vol == NULL || out == NULL) {
        return A2_ERR;
    }
    if (block >= vol->total_blocks) {
        return A2_ERR;
    }

    idx = hostfs_map_find(vol, (uint16_t)block);
    if (idx < 0) {
        memset(out, 0, HOSTFS_BLOCK_SIZE);
        return A2_OK;
    }

    if (vol->map[idx].kind == HOSTFS_MAP_RAM) {
        memcpy(out, vol->map[idx].u.ram, HOSTFS_BLOCK_SIZE);
        return A2_OK;
    }

    memset(out, 0, HOSTFS_BLOCK_SIZE);
    fp = fopen(vol->files[vol->map[idx].u.host.file_index].host_path, "rb");
    if (fp == NULL) {
        return A2_ERR;
    }
    if (fseek(fp, (long)vol->map[idx].u.host.offset, SEEK_SET) != 0) {
        fclose(fp);
        return A2_ERR;
    }
    got = fread(out, 1, HOSTFS_BLOCK_SIZE, fp);
    (void)got; /* short reads leave remaining zeros */
    fclose(fp);
    return A2_OK;
}

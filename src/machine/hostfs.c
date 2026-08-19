/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#include "hostfs.h"
#include "hostfs_boot.h"

#include "a2_status.h"
#include "apple2_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#define A2M_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define hostfs_ftruncate(fd, sz) _chsize_s(fd, (long long)(sz))
#define hostfs_stricmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#define A2M_STAT_ISDIR(mode) S_ISDIR(mode)
#define hostfs_ftruncate(fd, sz) ftruncate(fd, (off_t)(sz))
#define hostfs_stricmp strcasecmp
#endif

enum {
    HOSTFS_ENTRY_LENGTH = 39,
    HOSTFS_ENTRIES_PER_BLOCK = 13,
    HOSTFS_BITMAP_BLOCKS = 16,
    HOSTFS_ACCESS_FILE = 0xC3u,
    HOSTFS_ACCESS_VOL = 0xC3u,
    HOSTFS_POLL_PERIOD = 1000000u, /* ~1s at 1 MHz */
    HOSTFS_BASENAME_MAX = 256
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
    bool active;
    char prodos_name[HOSTFS_NAME_MAX];
    uint8_t name_len;
    uint8_t file_type;
    uint16_t aux_type;
    uint32_t eof;
    uint8_t storage_type;
    uint16_t key_block;
    uint16_t blocks_used;
    char host_path[HOSTFS_PATH_MAX];
    time_t host_mtime;
    uint64_t host_size;
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
    int file_slots; /* high-water of used slots (active or not) */

    hostfs_block_ref *map;
    int map_count;
    int map_cap;

    uint8_t *bitmap;
    uint64_t last_poll_cycles;
    int guest_write_depth;
    bool dirty;
    bool dir_full_warned;
    /* Last persisted catalog order (host basenames); used to avoid needless rewrites. */
    char order_basenames[HOSTFS_MAX_FILES][256];
    int order_count;
};

/* ---- small helpers ---- */

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
    int i;
    int n = 0;
    if (vol == NULL) {
        return 0;
    }
    for (i = 0; i < vol->file_slots; ++i) {
        if (vol->files[i].active) {
            n++;
        }
    }
    return n;
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
    if (o == 0u || !(out[0] >= 'A' && out[0] <= 'Z')) {
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
    const char *base;

    if (filename == NULL || filename[0] == '\0') {
        return false;
    }
    base = filename;
    {
        const char *slash = strrchr(filename, '/');
#ifdef _WIN32
        const char *b = strrchr(filename, '\\');
        if (b != NULL && (slash == NULL || b > slash)) {
            slash = b;
        }
#endif
        if (slash != NULL) {
            base = slash + 1;
        }
    }
    if (!apple2_naps_parse_path(base, &type, &aux)) {
        return false;
    }
    len = strlen(base);
    if (len < 7u) {
        return false;
    }
    len -= 7u;
    if (len >= sizeof(stem)) {
        len = sizeof(stem) - 1u;
    }
    memcpy(stem, base, len);
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

bool hostfs_compose_naps_filename(
    const char *name,
    uint8_t file_type,
    uint16_t aux_type,
    char *out,
    size_t out_size)
{
    char stem[HOSTFS_NAME_MAX];
    uint8_t ignored_type = 0;
    uint16_t ignored_aux = 0;
    const char *use_stem = name;

    if (name == NULL || name[0] == '\0' || out == NULL || out_size < 10u) {
        return false;
    }
    /* Assembler may already pass NAME#ttxxxx — observe stem, do not double-tag. */
    if (hostfs_naps_parse_name(name, stem, sizeof(stem), &ignored_type, &ignored_aux)) {
        use_stem = stem;
    }
    if (snprintf(out, out_size, "%s#%02X%04X", use_stem, (unsigned)file_type,
                 (unsigned)aux_type) >= (int)out_size) {
        return false;
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

static uint16_t hostfs_read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t hostfs_read_u24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static void hostfs_index_set(uint8_t *index, unsigned slot, uint16_t block)
{
    index[slot] = (uint8_t)(block & 0xFFu);
    index[256u + slot] = (uint8_t)((block >> 8) & 0xFFu);
}

static uint16_t hostfs_index_get(const uint8_t *index, unsigned slot)
{
    return (uint16_t)(index[slot] | ((uint16_t)index[256u + slot] << 8));
}

static void hostfs_bitmap_mark_used(uint8_t *bitmap, uint16_t block)
{
    size_t byte_index = (size_t)block / 8u;
    unsigned bit = 7u - ((unsigned)block % 8u);
    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] & (uint8_t)~(1u << bit));
}

static void hostfs_bitmap_mark_free(uint8_t *bitmap, uint16_t block)
{
    size_t byte_index = (size_t)block / 8u;
    unsigned bit = 7u - ((unsigned)block % 8u);
    bitmap[byte_index] = (uint8_t)(bitmap[byte_index] | (uint8_t)(1u << bit));
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

static int hostfs_map_grow(hostfs_volume *vol)
{
    int new_cap;
    hostfs_block_ref *n;
    if (vol->map_count < vol->map_cap) {
        return A2_OK;
    }
    new_cap = vol->map_cap == 0 ? 64 : vol->map_cap * 2;
    n = (hostfs_block_ref *)realloc(vol->map, (size_t)new_cap * sizeof(hostfs_block_ref));
    if (n == NULL) {
        return A2_ERR;
    }
    vol->map = n;
    vol->map_cap = new_cap;
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

static int hostfs_map_add_ram(hostfs_volume *vol, uint16_t block, const uint8_t *data)
{
    hostfs_block_ref *ref;
    uint8_t *ram;
    int idx = hostfs_map_find(vol, block);

    if (idx >= 0) {
        if (vol->map[idx].kind == HOSTFS_MAP_RAM) {
            if (data != NULL) {
                memcpy(vol->map[idx].u.ram, data, HOSTFS_BLOCK_SIZE);
            }
            return A2_OK;
        }
        /* Replace host mapping with RAM (rare). */
        vol->map[idx].kind = HOSTFS_MAP_RAM;
        ram = (uint8_t *)malloc(HOSTFS_BLOCK_SIZE);
        if (ram == NULL) {
            return A2_ERR;
        }
        if (data != NULL) {
            memcpy(ram, data, HOSTFS_BLOCK_SIZE);
        } else {
            memset(ram, 0, HOSTFS_BLOCK_SIZE);
        }
        vol->map[idx].u.ram = ram;
        return A2_OK;
    }
    if (hostfs_map_grow(vol) != A2_OK) {
        return A2_ERR;
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
    int idx = hostfs_map_find(vol, block);

    if (idx >= 0) {
        if (vol->map[idx].kind == HOSTFS_MAP_RAM) {
            free(vol->map[idx].u.ram);
        }
        vol->map[idx].kind = HOSTFS_MAP_HOST;
        vol->map[idx].u.host.file_index = file_index;
        vol->map[idx].u.host.offset = offset;
        return A2_OK;
    }
    if (hostfs_map_grow(vol) != A2_OK) {
        return A2_ERR;
    }
    ref = &vol->map[vol->map_count++];
    ref->block = block;
    ref->kind = HOSTFS_MAP_HOST;
    ref->u.host.file_index = file_index;
    ref->u.host.offset = offset;
    return A2_OK;
}

static void hostfs_map_remove_block(hostfs_volume *vol, uint16_t block)
{
    int idx = hostfs_map_find(vol, block);
    if (idx < 0) {
        return;
    }
    if (vol->map[idx].kind == HOSTFS_MAP_RAM) {
        free(vol->map[idx].u.ram);
    }
    vol->map[idx] = vol->map[vol->map_count - 1];
    vol->map_count--;
}

static void hostfs_map_remove_file(hostfs_volume *vol, int file_index)
{
    int i = 0;
    while (i < vol->map_count) {
        if (vol->map[i].kind == HOSTFS_MAP_HOST &&
            vol->map[i].u.host.file_index == file_index) {
            hostfs_bitmap_mark_free(vol->bitmap, vol->map[i].block);
            vol->map[i] = vol->map[vol->map_count - 1];
            vol->map_count--;
            continue;
        }
        ++i;
    }
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

static uint16_t hostfs_dir_blocks_for_files(int file_count)
{
    if (file_count <= 12) {
        return 1u;
    }
    return (uint16_t)(1u + ((uint16_t)(file_count - 12) + 12u) / 13u);
}

static int hostfs_active_name_exists(const hostfs_volume *vol, const char *name, int skip)
{
    int i;
    for (i = 0; i < vol->file_slots; ++i) {
        if (i == skip || !vol->files[i].active) {
            continue;
        }
        if (strcmp(vol->files[i].prodos_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hostfs_find_by_key(const hostfs_volume *vol, uint16_t key)
{
    int i;
    for (i = 0; i < vol->file_slots; ++i) {
        if (vol->files[i].active && vol->files[i].key_block == key) {
            return i;
        }
    }
    return -1;
}

static int hostfs_find_by_name(const hostfs_volume *vol, const char *name)
{
    int i;
    for (i = 0; i < vol->file_slots; ++i) {
        if (vol->files[i].active && strcmp(vol->files[i].prodos_name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int hostfs_alloc_file_slot(hostfs_volume *vol)
{
    int i;
    for (i = 0; i < vol->file_slots; ++i) {
        if (!vol->files[i].active) {
            return i;
        }
    }
    if (vol->file_slots >= HOSTFS_MAX_FILES) {
        return -1;
    }
    return vol->file_slots++;
}

/* ---- host file I/O ---- */

static int hostfs_host_write(
    hostfs_volume *vol, int file_index, uint32_t offset, const uint8_t *data, size_t len)
{
    FILE *fp;
    hostfs_file *file;
    long want_end;

    if (file_index < 0 || file_index >= vol->file_slots || !vol->files[file_index].active) {
        return A2_ERR;
    }
    file = &vol->files[file_index];
    fp = fopen(file->host_path, "rb+");
    if (fp == NULL) {
        fp = fopen(file->host_path, "wb+");
        if (fp == NULL) {
            return A2_ERR;
        }
    }
    want_end = (long)offset + (long)len;
    if (fseek(fp, 0, SEEK_END) == 0) {
        long cur = ftell(fp);
        if (cur >= 0 && cur < (long)offset) {
            /* Extend with zeros. */
            static const uint8_t z[512] = {0};
            while (cur < (long)offset) {
                long chunk = (long)offset - cur;
                if (chunk > 512) {
                    chunk = 512;
                }
                if (fwrite(z, 1, (size_t)chunk, fp) != (size_t)chunk) {
                    fclose(fp);
                    return A2_ERR;
                }
                cur += chunk;
            }
        }
    }
    if (fseek(fp, (long)offset, SEEK_SET) != 0 ||
        fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return A2_ERR;
    }
    fclose(fp);
    if ((uint32_t)want_end > file->eof) {
        /* Directory EOF is authoritative; grow tracking for host_size. */
        file->host_size = (uint64_t)want_end;
    }
    vol->dirty = true;
    return A2_OK;
}

static int hostfs_host_truncate(hostfs_volume *vol, int file_index, uint32_t eof)
{
    FILE *fp;
    hostfs_file *file;
    if (file_index < 0 || file_index >= vol->file_slots || !vol->files[file_index].active) {
        return A2_ERR;
    }
    file = &vol->files[file_index];
    fp = fopen(file->host_path, "rb+");
    if (fp == NULL) {
        return A2_ERR;
    }
    if (hostfs_ftruncate(fileno(fp), eof) != 0) {
        fclose(fp);
        return A2_ERR;
    }
    fclose(fp);
    file->eof = eof;
    file->host_size = eof;
    vol->dirty = true;
    return A2_OK;
}

static int hostfs_find_host_by_stem(
    const hostfs_volume *vol, const char *stem, char *out_path, size_t out_size)
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
        uint8_t type;
        uint16_t aux;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (!hostfs_naps_parse_name(data.cFileName, prodos, sizeof(prodos), &type, &aux)) {
            continue;
        }
        if (strcmp(prodos, stem) == 0) {
            hostfs_path_join(out_path, out_size, vol->root_path, data.cFileName);
            FindClose(handle);
            return A2_OK;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return A2_ERR;
#else
    DIR *dir = opendir(vol->root_path);
    struct dirent *entry;
    if (dir == NULL) {
        return A2_ERR;
    }
    while ((entry = readdir(dir)) != NULL) {
        char prodos[HOSTFS_NAME_MAX];
        uint8_t type;
        uint16_t aux;
        if (!hostfs_naps_parse_name(entry->d_name, prodos, sizeof(prodos), &type, &aux)) {
            continue;
        }
        if (strcmp(prodos, stem) == 0) {
            hostfs_path_join(out_path, out_size, vol->root_path, entry->d_name);
            closedir(dir);
            return A2_OK;
        }
    }
    closedir(dir);
    return A2_ERR;
#endif
}

static int hostfs_create_or_reuse_host_file(
    hostfs_volume *vol,
    const char *prodos_name,
    uint8_t file_type,
    uint16_t aux_type,
    char *out_path,
    size_t out_size)
{
    char naps[HOSTFS_PATH_MAX];
    char existing[HOSTFS_PATH_MAX];

    if (!hostfs_compose_naps_filename(prodos_name, file_type, aux_type, naps, sizeof(naps))) {
        return A2_ERR;
    }
    if (hostfs_find_host_by_stem(vol, prodos_name, existing, sizeof(existing)) == A2_OK) {
        /* Reuse assembler/host file with matching stem; rename tag if needed. */
        char *slash = strrchr(existing, '/');
#ifdef _WIN32
        char *b = strrchr(existing, '\\');
        if (b != NULL && (slash == NULL || b > slash)) {
            slash = b;
        }
#endif
        {
            const char *base = slash != NULL ? slash + 1 : existing;
            if (strcmp(base, naps) != 0) {
                char dest[HOSTFS_PATH_MAX];
                hostfs_path_join(dest, sizeof(dest), vol->root_path, naps);
                if (rename(existing, dest) == 0) {
                    snprintf(out_path, out_size, "%s", dest);
                } else {
                    snprintf(out_path, out_size, "%s", existing);
                }
            } else {
                snprintf(out_path, out_size, "%s", existing);
            }
        }
        return A2_OK;
    }
    hostfs_path_join(out_path, out_size, vol->root_path, naps);
    {
        FILE *fp = fopen(out_path, "wb");
        if (fp == NULL) {
            return A2_ERR;
        }
        fclose(fp);
    }
    return A2_OK;
}

/* ---- storage allocation ---- */

static int hostfs_alloc_file_storage(hostfs_volume *vol, hostfs_file *file, int file_index)
{
    uint32_t data_blocks;
    uint16_t blocks_used = 0;
    uint32_t i;

    data_blocks = (file->eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    if (file->eof == 0u) {
        data_blocks = 1u;
    }

    if (data_blocks <= 1u) {
        uint16_t key = hostfs_alloc_block(vol);
        if (key == 0u || key >= vol->total_blocks) {
            return A2_ERR;
        }
        file->storage_type = 1u;
        file->key_block = key;
        if (hostfs_map_add_host(vol, key, file_index, 0u) != A2_OK) {
            return A2_ERR;
        }
        hostfs_bitmap_mark_used(vol->bitmap, key);
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
        hostfs_bitmap_mark_used(vol->bitmap, key);
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
            hostfs_bitmap_mark_used(vol->bitmap, db);
            blocks_used++;
        }
    } else {
        uint16_t master = hostfs_alloc_block(vol);
        uint8_t *master_idx;
        uint32_t remaining;
        unsigned idx_slot = 0;

        if (data_blocks > 128u * 256u || master == 0u || master >= vol->total_blocks) {
            return A2_ERR;
        }
        if (hostfs_map_add_ram(vol, master, NULL) != A2_OK) {
            return A2_ERR;
        }
        hostfs_bitmap_mark_used(vol->bitmap, master);
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
            hostfs_bitmap_mark_used(vol->bitmap, ib);
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
                        vol, db, file_index, (uint32_t)(logical * HOSTFS_BLOCK_SIZE)) !=
                    A2_OK) {
                    return A2_ERR;
                }
                hostfs_bitmap_mark_used(vol->bitmap, db);
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
    entry[0x1C] = 0;
    entry[0x1D] = 0;
    entry[0x1E] = HOSTFS_ACCESS_FILE;
    hostfs_write_u16(entry + 0x1F, file->aux_type);
    hostfs_write_u16(entry + 0x25, header_ptr);
}

static int hostfs_patch_volume_file_count(hostfs_volume *vol)
{
    uint8_t *dir = hostfs_map_ram_ptr(vol, 2);
    if (dir == NULL) {
        return A2_ERR;
    }
    hostfs_write_u16(dir + 4 + 0x21, (uint16_t)hostfs_file_count(vol));
    return A2_OK;
}

static uint8_t *hostfs_find_dir_entry_slot(hostfs_volume *vol, const char *name, int *out_free)
{
    uint16_t d;
    if (out_free != NULL) {
        *out_free = 0;
    }
    for (d = 0; d < vol->dir_block_count; ++d) {
        uint16_t block = (uint16_t)(2u + d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            if (e[0] == 0) {
                if (out_free != NULL && *out_free == 0) {
                    *out_free = 1;
                    /* remember first free by returning NULL with flag; caller scans again */
                }
                continue;
            }
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            if (name != NULL && strcmp(nm, name) == 0) {
                return e;
            }
        }
    }
    return NULL;
}

static uint8_t *hostfs_first_free_dir_entry(hostfs_volume *vol)
{
    uint16_t d;
    for (d = 0; d < vol->dir_block_count; ++d) {
        uint16_t block = (uint16_t)(2u + d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            if (e[0] == 0) {
                return e;
            }
        }
    }
    return NULL;
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
            hostfs_write_u16(hdr + 0x21, (uint16_t)hostfs_file_count(vol));
            hostfs_write_u16(hdr + 0x23, vol->bitmap_block);
            hostfs_write_u16(hdr + 0x25, vol->total_blocks);
            for (slot = 1; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
                while (file_index < vol->file_slots && !vol->files[file_index].active) {
                    file_index++;
                }
                if (file_index >= vol->file_slots) {
                    break;
                }
                hostfs_fill_file_entry(
                    dir + 4 + slot * HOSTFS_ENTRY_LENGTH, &vol->files[file_index], 2u);
                file_index++;
            }
        } else {
            for (slot = 0; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
                while (file_index < vol->file_slots && !vol->files[file_index].active) {
                    file_index++;
                }
                if (file_index >= vol->file_slots) {
                    break;
                }
                hostfs_fill_file_entry(
                    dir + 4 + slot * HOSTFS_ENTRY_LENGTH, &vol->files[file_index], 2u);
                file_index++;
            }
        }
    }
    return A2_OK;
}

static int hostfs_sync_bitmap_to_map(hostfs_volume *vol)
{
    uint16_t b;
    for (b = 0; b < HOSTFS_BITMAP_BLOCKS; ++b) {
        uint16_t block = (uint16_t)(vol->bitmap_block + b);
        if (hostfs_map_add_ram(
                vol, block, vol->bitmap + (size_t)b * HOSTFS_BLOCK_SIZE) != A2_OK) {
            return A2_ERR;
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
    memset(vol->bitmap, 0xFF, bitmap_bytes);
    for (b = 0; b < vol->next_block; ++b) {
        hostfs_bitmap_mark_used(vol->bitmap, b);
    }
    for (i = 0; i < vol->map_count; ++i) {
        hostfs_bitmap_mark_used(vol->bitmap, vol->map[i].block);
    }
    return hostfs_sync_bitmap_to_map(vol);
}

/* ---- scan / mount / order manifest ---- */

typedef struct {
    char prodos[HOSTFS_NAME_MAX];
    uint8_t type;
    uint16_t aux;
    char host_path[HOSTFS_PATH_MAX];
    char basename[HOSTFS_BASENAME_MAX];
    uint64_t size;
    time_t mtime;
} hostfs_scan_ent;

static const char *hostfs_path_basename(const char *path)
{
    const char *slash;
    if (path == NULL) {
        return "";
    }
    slash = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *b = strrchr(path, '\\');
        if (b != NULL && (slash == NULL || b > slash)) {
            slash = b;
        }
    }
#endif
    return slash != NULL ? slash + 1 : path;
}

static int hostfs_basename_cmp(const char *a, const char *b)
{
    return hostfs_stricmp(a != NULL ? a : "", b != NULL ? b : "");
}

static int hostfs_scan_ent_basename_cmp(const void *pa, const void *pb)
{
    const hostfs_scan_ent *a = (const hostfs_scan_ent *)pa;
    const hostfs_scan_ent *b = (const hostfs_scan_ent *)pb;
    return hostfs_basename_cmp(a->basename, b->basename);
}

static int hostfs_collect_scans(hostfs_volume *vol, hostfs_scan_ent *out, int max_out)
{
    int count = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search[HOSTFS_PATH_MAX];
    hostfs_path_join(search, sizeof(search), vol->root_path, "*");
    handle = FindFirstFileA(search, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        struct stat st;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (hostfs_basename_cmp(data.cFileName, HOSTFS_ORDER_FILENAME) == 0) {
            continue;
        }
        if (count >= max_out) {
            break;
        }
        if (!hostfs_naps_parse_name(
                data.cFileName, out[count].prodos, sizeof(out[count].prodos),
                &out[count].type, &out[count].aux)) {
            continue;
        }
        hostfs_path_join(out[count].host_path, sizeof(out[count].host_path),
                         vol->root_path, data.cFileName);
        if (stat(out[count].host_path, &st) != 0) {
            continue;
        }
        snprintf(out[count].basename, sizeof(out[count].basename), "%s", data.cFileName);
        out[count].size = st.st_size < 0 ? 0u : (uint64_t)st.st_size;
        out[count].mtime = st.st_mtime;
        count++;
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR *dir = opendir(vol->root_path);
    struct dirent *entry;
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        if (hostfs_basename_cmp(entry->d_name, HOSTFS_ORDER_FILENAME) == 0) {
            continue;
        }
        if (count >= max_out) {
            break;
        }
        if (!hostfs_naps_parse_name(
                entry->d_name, out[count].prodos, sizeof(out[count].prodos),
                &out[count].type, &out[count].aux)) {
            continue;
        }
        hostfs_path_join(out[count].host_path, sizeof(out[count].host_path),
                         vol->root_path, entry->d_name);
        if (stat(out[count].host_path, &st) != 0 || A2M_STAT_ISDIR(st.st_mode)) {
            continue;
        }
        snprintf(out[count].basename, sizeof(out[count].basename), "%s", entry->d_name);
        out[count].size = st.st_size < 0 ? 0u : (uint64_t)st.st_size;
        out[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dir);
#endif
    return count;
}

static int hostfs_load_order_file(
    const char *root_path, char names[][HOSTFS_BASENAME_MAX], int max_names)
{
    char path[HOSTFS_PATH_MAX];
    FILE *fp;
    char line[HOSTFS_BASENAME_MAX + 32];
    int count = 0;

    hostfs_path_join(path, sizeof(path), root_path, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    while (count < max_names && fgets(line, (int)sizeof(line), fp) != NULL) {
        size_t len;
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0' || *p == '#' || *p == '\n' || *p == '\r') {
            continue;
        }
        len = strlen(p);
        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' ' ||
                           p[len - 1] == '\t')) {
            p[--len] = '\0';
        }
        if (len == 0 || len >= HOSTFS_BASENAME_MAX) {
            continue;
        }
        /* Only NAPS basenames belong in the order list. */
        {
            char prodos[HOSTFS_NAME_MAX];
            uint8_t type;
            uint16_t aux;
            if (!hostfs_naps_parse_name(p, prodos, sizeof(prodos), &type, &aux)) {
                continue;
            }
        }
        snprintf(names[count], HOSTFS_BASENAME_MAX, "%s", p);
        count++;
    }
    fclose(fp);
    return count;
}

static void hostfs_apply_order_to_scans(
    hostfs_scan_ent *scans, int n, const char names[][HOSTFS_BASENAME_MAX], int name_count)
{
    hostfs_scan_ent *ordered;
    hostfs_scan_ent *rest;
    bool used[HOSTFS_MAX_FILES];
    int out = 0;
    int rest_n = 0;
    int i;
    int k;

    if (n <= 0) {
        return;
    }
    ordered = (hostfs_scan_ent *)malloc((size_t)n * sizeof(hostfs_scan_ent));
    rest = (hostfs_scan_ent *)malloc((size_t)n * sizeof(hostfs_scan_ent));
    if (ordered == NULL || rest == NULL) {
        free(ordered);
        free(rest);
        return;
    }
    memset(used, 0, sizeof(used));

    for (k = 0; k < name_count; ++k) {
        for (i = 0; i < n; ++i) {
            if (used[i]) {
                continue;
            }
            if (hostfs_basename_cmp(scans[i].basename, names[k]) == 0) {
                ordered[out++] = scans[i];
                used[i] = true;
                break;
            }
        }
    }

    for (i = 0; i < n; ++i) {
        if (!used[i]) {
            rest[rest_n++] = scans[i];
        }
    }
    if (rest_n > 1) {
        qsort(rest, (size_t)rest_n, sizeof(rest[0]), hostfs_scan_ent_basename_cmp);
    }
    for (i = 0; i < rest_n; ++i) {
        ordered[out++] = rest[i];
    }

    memcpy(scans, ordered, (size_t)out * sizeof(scans[0]));
    free(ordered);
    free(rest);
}

static int hostfs_catalog_basenames(
    hostfs_volume *vol, char names[][HOSTFS_BASENAME_MAX], int max_names)
{
    int count = 0;
    uint16_t d;

    for (d = 0; d < vol->dir_block_count && count < max_names; ++d) {
        uint8_t *dir = hostfs_map_ram_ptr(vol, (uint16_t)(2u + d));
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK && count < max_names; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t st;
            uint8_t nl;
            char nm[HOSTFS_NAME_MAX];
            int fi;
            if (e[0] == 0) {
                continue;
            }
            st = (uint8_t)(e[0] >> 4);
            if (st < 1u || st > 3u) {
                continue;
            }
            nl = (uint8_t)(e[0] & 0x0Fu);
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            fi = hostfs_find_by_name(vol, nm);
            if (fi < 0) {
                uint16_t key = hostfs_read_u16(e + 0x11);
                fi = hostfs_find_by_key(vol, key);
            }
            if (fi < 0 || !vol->files[fi].active) {
                continue;
            }
            snprintf(
                names[count], HOSTFS_BASENAME_MAX, "%s",
                hostfs_path_basename(vol->files[fi].host_path));
            count++;
        }
    }
    return count;
}

static bool hostfs_order_lists_equal(
    const char a[][HOSTFS_BASENAME_MAX],
    int a_count,
    const char b[][HOSTFS_BASENAME_MAX],
    int b_count)
{
    int i;
    if (a_count != b_count) {
        return false;
    }
    for (i = 0; i < a_count; ++i) {
        if (hostfs_basename_cmp(a[i], b[i]) != 0) {
            return false;
        }
    }
    return true;
}

static int hostfs_persist_order_manifest(hostfs_volume *vol)
{
    char (*current)[HOSTFS_BASENAME_MAX];
    int count;
    char path[HOSTFS_PATH_MAX];
    FILE *fp;
    int i;

    if (vol == NULL) {
        return A2_ERR;
    }
    current = (char (*)[HOSTFS_BASENAME_MAX])calloc(
        (size_t)HOSTFS_MAX_FILES, HOSTFS_BASENAME_MAX);
    if (current == NULL) {
        return A2_ERR;
    }
    count = hostfs_catalog_basenames(vol, current, HOSTFS_MAX_FILES);
    if (hostfs_order_lists_equal(current, count, vol->order_basenames, vol->order_count)) {
        free(current);
        return A2_OK;
    }

    hostfs_path_join(path, sizeof(path), vol->root_path, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "w");
    if (fp == NULL) {
        free(current);
        return A2_ERR;
    }
    fprintf(fp, "# a2m HostFS catalog order (one NAPS basename per line)\n");
    for (i = 0; i < count; ++i) {
        fprintf(fp, "%s\n", current[i]);
    }
    fclose(fp);

    vol->order_count = count;
    for (i = 0; i < count; ++i) {
        snprintf(vol->order_basenames[i], HOSTFS_BASENAME_MAX, "%s", current[i]);
    }
    free(current);
    return A2_OK;
}

static int hostfs_scan_into_files(hostfs_volume *vol)
{
    hostfs_scan_ent *scans;
    char (*order_names)[HOSTFS_BASENAME_MAX];
    int order_count;
    int n;
    int i;
    int rc = A2_ERR;

    scans = (hostfs_scan_ent *)calloc((size_t)HOSTFS_MAX_FILES, sizeof(hostfs_scan_ent));
    order_names =
        (char (*)[HOSTFS_BASENAME_MAX])calloc((size_t)HOSTFS_MAX_FILES, HOSTFS_BASENAME_MAX);
    if (scans == NULL || order_names == NULL) {
        free(scans);
        free(order_names);
        return A2_ERR;
    }

    n = hostfs_collect_scans(vol, scans, HOSTFS_MAX_FILES);
    if (n < 0) {
        goto done;
    }
    order_count = hostfs_load_order_file(vol->root_path, order_names, HOSTFS_MAX_FILES);
    if (order_count > 0) {
        hostfs_apply_order_to_scans(scans, n, order_names, order_count);
    } else if (n > 1) {
        /* No manifest: deterministic alpha by basename (not readdir order). */
        qsort(scans, (size_t)n, sizeof(scans[0]), hostfs_scan_ent_basename_cmp);
    }

    vol->file_slots = 0;
    vol->order_count = 0;
    for (i = 0; i < n; ++i) {
        hostfs_file *file;
        int j;
        int dup = 0;
        for (j = 0; j < i; ++j) {
            if (strcmp(scans[j].prodos, scans[i].prodos) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        file = &vol->files[vol->file_slots++];
        memset(file, 0, sizeof(*file));
        file->active = true;
        snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", scans[i].prodos);
        file->name_len = (uint8_t)strlen(file->prodos_name);
        file->file_type = scans[i].type;
        file->aux_type = scans[i].aux;
        file->eof = scans[i].size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)scans[i].size;
        file->host_size = scans[i].size;
        file->host_mtime = scans[i].mtime;
        snprintf(file->host_path, sizeof(file->host_path), "%s", scans[i].host_path);
        if (vol->order_count < HOSTFS_MAX_FILES) {
            snprintf(
                vol->order_basenames[vol->order_count], HOSTFS_BASENAME_MAX, "%s",
                scans[i].basename);
            vol->order_count++;
        }
    }
    rc = A2_OK;

done:
    free(scans);
    free(order_names);
    return rc;
}

void hostfs_eject(hostfs_volume *vol)
{
    int i;
    if (vol == NULL) {
        return;
    }
    (void)hostfs_flush(vol);
    for (i = 0; i < vol->map_count; ++i) {
        if (vol->map[i].kind == HOSTFS_MAP_RAM) {
            free(vol->map[i].u.ram);
        }
    }
    free(vol->map);
    free(vol->bitmap);
    free(vol);
}

int hostfs_flush(hostfs_volume *vol)
{
    if (vol == NULL) {
        return A2_ERR;
    }
    (void)hostfs_persist_order_manifest(vol);
    vol->dirty = false;
    return A2_OK;
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

    if (hostfs_scan_into_files(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }

    vol->dir_block_count = hostfs_dir_blocks_for_files(hostfs_file_count(vol));
    /* Leave spare directory capacity for Phase 2 adds when possible. */
    if (vol->dir_block_count < 2u && hostfs_file_count(vol) > 0) {
        vol->dir_block_count = 2u;
    }
    vol->bitmap_block = (uint16_t)(2u + vol->dir_block_count);

    vol->next_block = 0;
    (void)hostfs_alloc_block(vol);
    (void)hostfs_alloc_block(vol);
    for (b = 0; b < vol->dir_block_count; ++b) {
        (void)hostfs_alloc_block(vol);
    }
    for (b = 0; b < HOSTFS_BITMAP_BLOCKS; ++b) {
        (void)hostfs_alloc_block(vol);
    }

    if (hostfs_map_add_ram(vol, 0, hostfs_boot_block0) != A2_OK ||
        hostfs_map_add_ram(vol, 1, NULL) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }

    /* Bitmap buffer before file alloc so marks work. */
    {
        size_t bitmap_bytes = (size_t)HOSTFS_BITMAP_BLOCKS * HOSTFS_BLOCK_SIZE;
        vol->bitmap = (uint8_t *)malloc(bitmap_bytes);
        if (vol->bitmap == NULL) {
            hostfs_eject(vol);
            return NULL;
        }
        memset(vol->bitmap, 0xFF, bitmap_bytes);
        for (b = 0; b < vol->next_block; ++b) {
            hostfs_bitmap_mark_used(vol->bitmap, b);
        }
    }

    for (i = 0; i < vol->file_slots; ++i) {
        if (!vol->files[i].active) {
            continue;
        }
        if (hostfs_alloc_file_storage(vol, &vol->files[i], i) != A2_OK) {
            hostfs_eject(vol);
            return NULL;
        }
    }

    if (hostfs_build_directory(vol) != A2_OK || hostfs_sync_bitmap_to_map(vol) != A2_OK) {
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

    if (vol == NULL || out == NULL || block >= vol->total_blocks) {
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
    {
        int fi = vol->map[idx].u.host.file_index;
        if (fi < 0 || fi >= vol->file_slots || !vol->files[fi].active) {
            return A2_ERR;
        }
        fp = fopen(vol->files[fi].host_path, "rb");
        if (fp == NULL) {
            return A2_ERR;
        }
        if (fseek(fp, (long)vol->map[idx].u.host.offset, SEEK_SET) != 0) {
            fclose(fp);
            return A2_ERR;
        }
        got = fread(out, 1, HOSTFS_BLOCK_SIZE, fp);
        (void)got;
        fclose(fp);
    }
    return A2_OK;
}

/* ---- bind guest-written blocks to a host file ---- */

static int hostfs_flush_ram_block_to_host(
    hostfs_volume *vol, uint16_t block, int file_index, uint32_t offset)
{
    int idx = hostfs_map_find(vol, block);
    uint8_t *ram;
    if (idx >= 0 && vol->map[idx].kind == HOSTFS_MAP_RAM) {
        ram = vol->map[idx].u.ram;
        if (hostfs_host_write(vol, file_index, offset, ram, HOSTFS_BLOCK_SIZE) != A2_OK) {
            return A2_ERR;
        }
        free(ram);
        vol->map[idx].kind = HOSTFS_MAP_HOST;
        vol->map[idx].u.host.file_index = file_index;
        vol->map[idx].u.host.offset = offset;
        return A2_OK;
    }
    return hostfs_map_add_host(vol, block, file_index, offset);
}

static int hostfs_bind_storage_to_host(hostfs_volume *vol, int file_index)
{
    hostfs_file *file = &vol->files[file_index];
    uint8_t st = file->storage_type;

    if (st == 1u) {
        return hostfs_flush_ram_block_to_host(vol, file->key_block, file_index, 0u);
    }
    if (st == 2u) {
        uint8_t *index = hostfs_map_ram_ptr(vol, file->key_block);
        uint32_t n;
        uint32_t i;
        if (index == NULL) {
            /* Key may already be unknown — ensure RAM placeholder */
            if (hostfs_map_add_ram(vol, file->key_block, NULL) != A2_OK) {
                return A2_ERR;
            }
            index = hostfs_map_ram_ptr(vol, file->key_block);
        }
        n = (file->eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
        if (n == 0u) {
            n = 1u;
        }
        for (i = 0; i < n && i < 256u; ++i) {
            uint16_t db = hostfs_index_get(index, (unsigned)i);
            if (db == 0u) {
                continue;
            }
            if (hostfs_flush_ram_block_to_host(
                    vol, db, file_index, (uint32_t)(i * HOSTFS_BLOCK_SIZE)) != A2_OK) {
                return A2_ERR;
            }
        }
        return A2_OK;
    }
    if (st == 3u) {
        uint8_t *master = hostfs_map_ram_ptr(vol, file->key_block);
        uint32_t logical = 0;
        unsigned s;
        if (master == NULL) {
            if (hostfs_map_add_ram(vol, file->key_block, NULL) != A2_OK) {
                return A2_ERR;
            }
            master = hostfs_map_ram_ptr(vol, file->key_block);
        }
        for (s = 0; s < 128u; ++s) {
            uint16_t ib = hostfs_index_get(master, s);
            uint8_t *index;
            unsigned j;
            if (ib == 0u) {
                continue;
            }
            index = hostfs_map_ram_ptr(vol, ib);
            if (index == NULL) {
                if (hostfs_map_add_ram(vol, ib, NULL) != A2_OK) {
                    return A2_ERR;
                }
                index = hostfs_map_ram_ptr(vol, ib);
            }
            for (j = 0; j < 256u; ++j) {
                uint16_t db = hostfs_index_get(index, j);
                if (db == 0u) {
                    logical++;
                    continue;
                }
                if (hostfs_flush_ram_block_to_host(
                        vol, db, file_index, (uint32_t)(logical * HOSTFS_BLOCK_SIZE)) !=
                    A2_OK) {
                    return A2_ERR;
                }
                logical++;
            }
        }
        return A2_OK;
    }
    return A2_OK;
}

/* ---- directory reconcile (Phase 3) ---- */

static int hostfs_reconcile_directory(hostfs_volume *vol)
{
    typedef struct {
        bool used;
        uint8_t storage_type;
        char name[HOSTFS_NAME_MAX];
        uint8_t name_len;
        uint8_t file_type;
        uint16_t key_block;
        uint16_t blocks_used;
        uint32_t eof;
        uint16_t aux_type;
    } dent;

    dent ents[HOSTFS_MAX_FILES];
    int ent_count = 0;
    uint16_t d;
    int i;
    bool seen[HOSTFS_MAX_FILES];

    memset(ents, 0, sizeof(ents));
    memset(seen, 0, sizeof(seen));

    for (d = 0; d < vol->dir_block_count; ++d) {
        uint8_t *dir = hostfs_map_ram_ptr(vol, (uint16_t)(2u + d));
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t st;
            dent *de;
            if (e[0] == 0 || ent_count >= HOSTFS_MAX_FILES) {
                continue;
            }
            st = (uint8_t)(e[0] >> 4);
            if (st == 0x0Fu || st == 0x0Du) {
                continue; /* volume header / subdirectory */
            }
            if (st < 1u || st > 3u) {
                continue;
            }
            de = &ents[ent_count++];
            de->used = true;
            de->storage_type = st;
            de->name_len = (uint8_t)(e[0] & 0x0Fu);
            memcpy(de->name, e + 1, de->name_len);
            de->name[de->name_len] = '\0';
            de->file_type = e[0x10];
            de->key_block = hostfs_read_u16(e + 0x11);
            de->blocks_used = hostfs_read_u16(e + 0x13);
            de->eof = hostfs_read_u24(e + 0x15);
            de->aux_type = hostfs_read_u16(e + 0x1F);
        }
    }

    /* Update / create from directory entries. */
    for (i = 0; i < ent_count; ++i) {
        dent *de = &ents[i];
        int fi = hostfs_find_by_key(vol, de->key_block);
        if (fi < 0) {
            fi = hostfs_find_by_name(vol, de->name);
        }
        if (fi < 0) {
            hostfs_file *file;
            fi = hostfs_alloc_file_slot(vol);
            if (fi < 0) {
                continue;
            }
            file = &vol->files[fi];
            memset(file, 0, sizeof(*file));
            file->active = true;
            snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", de->name);
            file->name_len = de->name_len;
            file->file_type = de->file_type;
            file->aux_type = de->aux_type;
            file->eof = de->eof;
            file->storage_type = de->storage_type;
            file->key_block = de->key_block;
            file->blocks_used = de->blocks_used;
            if (hostfs_create_or_reuse_host_file(
                    vol, de->name, de->file_type, de->aux_type, file->host_path,
                    sizeof(file->host_path)) != A2_OK) {
                file->active = false;
                continue;
            }
            (void)hostfs_bind_storage_to_host(vol, fi);
            if (de->eof > 0u) {
                (void)hostfs_host_truncate(vol, fi, de->eof);
            }
            seen[fi] = true;
        } else {
            hostfs_file *file = &vol->files[fi];
            seen[fi] = true;
            if (strcmp(file->prodos_name, de->name) != 0 ||
                file->file_type != de->file_type || file->aux_type != de->aux_type) {
                char naps[HOSTFS_PATH_MAX];
                char dest[HOSTFS_PATH_MAX];
                if (hostfs_compose_naps_filename(
                        de->name, de->file_type, de->aux_type, naps, sizeof(naps))) {
                    hostfs_path_join(dest, sizeof(dest), vol->root_path, naps);
                    if (strcmp(file->host_path, dest) != 0) {
                        (void)rename(file->host_path, dest);
                        snprintf(file->host_path, sizeof(file->host_path), "%s", dest);
                    }
                }
                snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", de->name);
                file->name_len = de->name_len;
                file->file_type = de->file_type;
                file->aux_type = de->aux_type;
            }
            if (de->eof < file->eof) {
                (void)hostfs_host_truncate(vol, fi, de->eof);
            }
            file->eof = de->eof;
            file->storage_type = de->storage_type;
            file->key_block = de->key_block;
            file->blocks_used = de->blocks_used;
            (void)hostfs_bind_storage_to_host(vol, fi);
        }
    }

    /* Destroy files missing from directory. */
    for (i = 0; i < vol->file_slots; ++i) {
        if (!vol->files[i].active || seen[i]) {
            continue;
        }
        (void)remove(vol->files[i].host_path);
        hostfs_map_remove_file(vol, i);
        vol->files[i].active = false;
        vol->files[i].host_path[0] = '\0';
    }

    (void)hostfs_patch_volume_file_count(vol);
    (void)hostfs_sync_bitmap_to_map(vol);
    (void)hostfs_persist_order_manifest(vol);
    return A2_OK;
}

int hostfs_write_block(hostfs_volume *vol, uint32_t block, const uint8_t *data)
{
    int idx;
    int rc = A2_OK;

    if (vol == NULL || data == NULL || block >= vol->total_blocks) {
        return A2_ERR;
    }

    vol->guest_write_depth++;

    idx = hostfs_map_find(vol, (uint16_t)block);
    if (idx >= 0 && vol->map[idx].kind == HOSTFS_MAP_HOST) {
        rc = hostfs_host_write(
            vol, vol->map[idx].u.host.file_index, vol->map[idx].u.host.offset, data,
            HOSTFS_BLOCK_SIZE);
    } else {
        /* Meta, index, orphan, or newly allocated block — keep in RAM. */
        rc = hostfs_map_add_ram(vol, (uint16_t)block, data);
        if (rc == A2_OK && vol->bitmap != NULL &&
            block >= vol->bitmap_block &&
            block < (uint32_t)vol->bitmap_block + HOSTFS_BITMAP_BLOCKS) {
            memcpy(
                vol->bitmap + (size_t)(block - vol->bitmap_block) * HOSTFS_BLOCK_SIZE,
                data,
                HOSTFS_BLOCK_SIZE);
        }
        if (block >= vol->next_block && block < vol->total_blocks) {
            vol->next_block = (uint16_t)(block + 1u);
        }
        hostfs_bitmap_mark_used(vol->bitmap, (uint16_t)block);
    }

    if (rc == A2_OK && block >= 2u && block < (uint32_t)(2u + vol->dir_block_count)) {
        rc = hostfs_reconcile_directory(vol);
    }

    vol->guest_write_depth--;
    return rc;
}

/* ---- Phase 2 host → volume ---- */

static int hostfs_grow_file_storage(hostfs_volume *vol, int file_index, uint32_t new_eof)
{
    hostfs_file *file = &vol->files[file_index];
    uint32_t old_blocks =
        (file->eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    uint32_t new_blocks = (new_eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    uint8_t need_st;

    if (new_eof == 0u) {
        new_blocks = 1u;
    }
    if (old_blocks == 0u) {
        old_blocks = 1u;
    }
    if (new_blocks <= 1u) {
        need_st = 1u;
    } else if (new_blocks <= 256u) {
        need_st = 2u;
    } else {
        need_st = 3u;
    }

    if (need_st != file->storage_type || new_blocks < old_blocks) {
        /* Reallocate this file only. */
        hostfs_map_remove_file(vol, file_index);
        file->eof = new_eof;
        if (hostfs_alloc_file_storage(vol, file, file_index) != A2_OK) {
            return A2_ERR;
        }
    } else if (new_blocks > old_blocks && file->storage_type == 2u) {
        uint8_t *index = hostfs_map_ram_ptr(vol, file->key_block);
        uint32_t i;
        if (index == NULL) {
            return A2_ERR;
        }
        for (i = old_blocks; i < new_blocks; ++i) {
            uint16_t db = hostfs_alloc_block(vol);
            if (db == 0u) {
                return A2_ERR;
            }
            hostfs_index_set(index, (unsigned)i, db);
            if (hostfs_map_add_host(
                    vol, db, file_index, (uint32_t)(i * HOSTFS_BLOCK_SIZE)) != A2_OK) {
                return A2_ERR;
            }
            hostfs_bitmap_mark_used(vol->bitmap, db);
            file->blocks_used++;
        }
        file->eof = new_eof;
    } else if (new_blocks > old_blocks && file->storage_type == 1u && need_st == 1u) {
        file->eof = new_eof;
    } else {
        file->eof = new_eof;
    }

    {
        uint8_t *entry = hostfs_find_dir_entry_slot(vol, file->prodos_name, NULL);
        if (entry != NULL) {
            hostfs_fill_file_entry(entry, file, 2u);
        }
    }
    (void)hostfs_sync_bitmap_to_map(vol);
    return A2_OK;
}

static int hostfs_add_file_from_scan(hostfs_volume *vol, const hostfs_scan_ent *sc)
{
    int fi;
    hostfs_file *file;
    uint8_t *entry;

    if (hostfs_active_name_exists(vol, sc->prodos, -1)) {
        return A2_OK;
    }
    entry = hostfs_first_free_dir_entry(vol);
    if (entry == NULL) {
        if (!vol->dir_full_warned) {
            fprintf(stderr,
                    "a2m: HostFS directory full; remount to pick up new files\n");
            vol->dir_full_warned = true;
        }
        return A2_ERR;
    }
    fi = hostfs_alloc_file_slot(vol);
    if (fi < 0) {
        return A2_ERR;
    }
    file = &vol->files[fi];
    memset(file, 0, sizeof(*file));
    file->active = true;
    snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", sc->prodos);
    file->name_len = (uint8_t)strlen(file->prodos_name);
    file->file_type = sc->type;
    file->aux_type = sc->aux;
    file->eof = sc->size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)sc->size;
    file->host_size = sc->size;
    file->host_mtime = sc->mtime;
    snprintf(file->host_path, sizeof(file->host_path), "%s", sc->host_path);
    if (hostfs_alloc_file_storage(vol, file, fi) != A2_OK) {
        file->active = false;
        return A2_ERR;
    }
    hostfs_fill_file_entry(entry, file, 2u);
    (void)hostfs_patch_volume_file_count(vol);
    (void)hostfs_sync_bitmap_to_map(vol);
    return A2_OK;
}

int hostfs_rescan(hostfs_volume *vol)
{
    hostfs_scan_ent *scans;
    int n;
    int i;
    bool matched[HOSTFS_MAX_FILES];

    if (vol == NULL) {
        return A2_ERR;
    }
    if (vol->guest_write_depth > 0) {
        return A2_OK;
    }

    scans = (hostfs_scan_ent *)calloc((size_t)HOSTFS_MAX_FILES, sizeof(hostfs_scan_ent));
    if (scans == NULL) {
        return A2_ERR;
    }
    n = hostfs_collect_scans(vol, scans, HOSTFS_MAX_FILES);
    if (n < 0) {
        free(scans);
        return A2_ERR;
    }
    memset(matched, 0, sizeof(matched));

    for (i = 0; i < n; ++i) {
        int fi = hostfs_find_by_name(vol, scans[i].prodos);
        if (fi < 0) {
            if (hostfs_add_file_from_scan(vol, &scans[i]) != A2_OK) {
                /* keep going; other files may still update */
            } else {
                fi = hostfs_find_by_name(vol, scans[i].prodos);
                if (fi >= 0) {
                    matched[fi] = true;
                }
            }
            continue;
        }
        matched[fi] = true;
        {
            hostfs_file *file = &vol->files[fi];
            uint32_t new_eof =
                scans[i].size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)scans[i].size;
            /* Path/tag may change (assembler rewrite). */
            if (strcmp(file->host_path, scans[i].host_path) != 0) {
                snprintf(file->host_path, sizeof(file->host_path), "%s", scans[i].host_path);
            }
            file->file_type = scans[i].type;
            file->aux_type = scans[i].aux;
            file->host_mtime = scans[i].mtime;
            file->host_size = scans[i].size;
            if (new_eof != file->eof) {
                (void)hostfs_grow_file_storage(vol, fi, new_eof);
            } else {
                uint8_t *entry = hostfs_find_dir_entry_slot(vol, file->prodos_name, NULL);
                if (entry != NULL) {
                    hostfs_fill_file_entry(entry, file, 2u);
                }
            }
        }
    }

    for (i = 0; i < vol->file_slots; ++i) {
        if (!vol->files[i].active || matched[i]) {
            continue;
        }
        {
            uint8_t *entry =
                hostfs_find_dir_entry_slot(vol, vol->files[i].prodos_name, NULL);
            if (entry != NULL) {
                memset(entry, 0, HOSTFS_ENTRY_LENGTH);
            }
        }
        hostfs_map_remove_file(vol, i);
        vol->files[i].active = false;
        vol->files[i].host_path[0] = '\0';
    }
    (void)hostfs_patch_volume_file_count(vol);
    (void)hostfs_sync_bitmap_to_map(vol);
    (void)hostfs_persist_order_manifest(vol);
    free(scans);
    return A2_OK;
}

void hostfs_poll(hostfs_volume *vol, uint64_t cycles)
{
    if (vol == NULL || vol->guest_write_depth > 0) {
        return;
    }
    if (vol->last_poll_cycles != 0u &&
        cycles - vol->last_poll_cycles < (uint64_t)HOSTFS_POLL_PERIOD) {
        return;
    }
    vol->last_poll_cycles = cycles;
    (void)hostfs_rescan(vol);
}

/* HostFS — ProDOS volume backed by a host directory (SmartPort media).
   Stefan Wessels, 2026. Public domain. */

#include "hostfs.h"
#include "hostfs_boot.h"

#include "a2_status.h"
#include "apple2.h"
#include "apple2_file.h"
#include "fs_watch.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <windows.h>
#define A2M_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define hostfs_ftruncate(fd, sz) _chsize_s(fd, (long long)(sz))
#define hostfs_stricmp _stricmp
#define hostfs_mkdir(path) _mkdir(path)
#define hostfs_rmdir(path) _rmdir(path)
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#define A2M_STAT_ISDIR(mode) S_ISDIR(mode)
#define hostfs_ftruncate(fd, sz) ftruncate(fd, (off_t)(sz))
#define hostfs_stricmp strcasecmp
#define hostfs_mkdir(path) mkdir((path), 0755)
#define hostfs_rmdir(path) rmdir(path)
#endif

enum {
    HOSTFS_BITMAP_BLOCKS = 16,
    HOSTFS_WARN_MAX = 256, /* longest diagnostic line retained for tests */
    HOSTFS_ACCESS_FILE = 0xC3u,
    HOSTFS_ACCESS_VOL = 0xC3u,
    HOSTFS_ACCESS_DIR = 0xC3u,
    HOSTFS_REFRESH_PERIOD_MS = 1000u, /* wall-clock; turbo-safe */
    HOSTFS_BASENAME_MAX = 256,
    HOSTFS_FILE_TYPE_DIR = 0x0Fu,
    HOSTFS_STOR_SEEDLING = 0x01u,
    HOSTFS_STOR_SAPLING = 0x02u,
    HOSTFS_STOR_TREE = 0x03u,
    HOSTFS_STOR_SUBDIR = 0x0Du,
    HOSTFS_STOR_SUBDIR_HDR = 0x0Eu,
    HOSTFS_STOR_VOL_HDR = 0x0Fu
};

typedef enum {
    HOSTFS_MAP_RAM = 1,
    HOSTFS_MAP_HOST = 2
} hostfs_map_kind;

typedef enum {
    HOSTFS_KIND_FILE = 1,
    HOSTFS_KIND_DIR = 2
} hostfs_kind;

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

/* One catalog entry. The host path is NOT stored: it is rebuilt on demand by
   walking parent_index (bounded by HOSTFS_MAX_DEPTH) and joining each ancestor's
   host basename, which lives in the volume's string arena at name_off. Storing
   it cost 1024 of the old 1088 bytes per node and capped the practical volume
   size at a few thousand entries.

   Nodes are referenced by int index everywhere - parent_index, the block map's
   file_index, dir_index - and never by stored pointer, so the table survives
   realloc. Keep it that way. */
typedef struct {
    int64_t host_mtime;
    uint32_t name_off; /* host basename, offset into vol->names */
    int32_t parent_index; /* -1 = volume root */
    uint32_t eof;
    uint32_t host_size;
    uint16_t aux_type;
    uint16_t key_block;
    uint16_t blocks_used;
    uint16_t dir_block_count; /* directories only */
    uint16_t parent_entry_block; /* block in parent that holds our entry */
    uint8_t active;
    uint8_t kind; /* hostfs_kind */
    uint8_t name_len;
    uint8_t file_type;
    uint8_t storage_type;
    uint8_t parent_entry_number; /* 1-based entry index within that block */
    char prodos_name[HOSTFS_NAME_MAX];
} hostfs_node;

typedef struct {
    uint32_t flags;
    char relative_path[FS_WATCH_PATH_MAX];
} hostfs_refresh_event;

struct hostfs_volume {
    char root_path[HOSTFS_PATH_MAX];
    char volume_name[HOSTFS_NAME_MAX];
    uint8_t volume_name_len;
    uint16_t total_blocks;
    uint16_t bitmap_block;
    uint16_t dir_block_count;
    uint16_t next_block;

    hostfs_node *nodes;
    int node_count; /* high-water of used slots (active or not) */
    int node_cap;

    /* Append-only arena of host basenames. Offset 0 is the empty string, so a
       zeroed node reads as "". Rename appends and abandons the old text; the
       waste is bounded because a remount rebuilds the arena from scratch. */
    char *names;
    size_t names_len;
    size_t names_cap;

    hostfs_block_ref *map;
    int map_count;
    int map_cap;
    int32_t *block_to_map; /* block number -> map slot, -1 = unmapped */

    uint8_t *bitmap;
    uint64_t last_refresh_ms; /* 0 = never stamped (mount sets after initial scan) */
    fs_watch *watch;
    bool periodic_refresh;
    bool synthetic_refresh;
    bool watch_coverage_failed;
    bool watch_unavailable_warned;
    bool refresh_rescan_required;
    bool internal_full_rescan_required;
    hostfs_refresh_event *synthetic_events;
    int synthetic_event_count;
    int synthetic_event_cap;
    int *dirty_parents;
    int dirty_parent_count;
    int dirty_parent_cap;
    bool dirty_root;
    int targeted_file_stats;
    int targeted_directory_scans;
    int full_rescans;
    int guest_write_depth;
    struct apple2 *apple;
    int slot;
    int device;
    int host_directory_changed;
    bool dirty;
    /* Warning suppression is per cause: one shared flag would let whichever fired
       first silence the others for the life of the mount. */
    bool root_full_warned;
    bool subdir_full_warned;
    bool order_frozen_warned;
    /* Host root holds more than HOSTFS_ROOT_MAX_ENTRIES entries, so the catalog is
       a truncated view of it. Sticky: set by the mount scan or by a post-mount add
       that found the root full, and never cleared while mounted. */
    bool root_truncated;
    bool node_limit_warned;
    int warn_count;
    char last_warning[HOSTFS_WARN_MAX];
    /* Last persisted catalog order (host basenames); used to avoid needless
       rewrites. Root-only, so it is bounded by the ProDOS volume directory. */
    char order_basenames[HOSTFS_ROOT_MAX_ENTRIES][HOSTFS_BASENAME_MAX];
    int order_count;
};

/* ---- small helpers ---- */

static bool hostfs_should_skip_basename(const char *name);
static const char *hostfs_path_basename(const char *path);

/* Single seam for every HostFS diagnostic. Writes one "a2m: HostFS ..." line to
   stderr and records it on the volume so tests can assert a warning fired without
   capturing stderr. Callers own suppression; this always reports. */
static void hostfs_warn(hostfs_volume *vol, const char *fmt, ...)
{
    char msg[HOSTFS_WARN_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf(stderr, "a2m: HostFS %s\n", msg);
    if (vol != NULL) {
        snprintf(vol->last_warning, sizeof(vol->last_warning), "%s", msg);
        vol->warn_count++;
    }
}

int hostfs_warning_count(const hostfs_volume *vol)
{
    return vol == NULL ? 0 : vol->warn_count;
}

const char *hostfs_last_warning(const hostfs_volume *vol)
{
    return vol == NULL ? "" : vol->last_warning;
}

static void hostfs_enter_periodic_refresh(hostfs_volume *vol)
{
    if (vol == NULL || vol->synthetic_refresh) {
        return;
    }
    fs_watch_destroy(vol->watch);
    vol->watch = NULL;
    vol->periodic_refresh = true;
    vol->watch_coverage_failed = false;
    if (!vol->watch_unavailable_warned) {
        hostfs_warn(
            vol, "%s: filesystem notifications unavailable; using periodic full refresh",
            vol->root_path);
        vol->watch_unavailable_warned = true;
    }
}

static bool hostfs_refresh_path_valid(const char *relative_path)
{
    const char *part;

    if (relative_path == NULL || relative_path[0] == '/' ||
        relative_path[0] == '\\' || strlen(relative_path) >= FS_WATCH_PATH_MAX) {
        return false;
    }
#if defined(_WIN32)
    if (relative_path[0] != '\0' && relative_path[1] == ':') {
        return false;
    }
#endif
    part = relative_path;
    while (*part != '\0') {
        const char *end = strchr(part, '/');
        size_t len = end != NULL ? (size_t)(end - part) : strlen(part);
        if ((len == 1u && part[0] == '.') ||
            (len == 2u && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (end == NULL) {
            break;
        }
        part = end + 1;
    }
    return true;
}

void hostfs_test_use_synthetic_events(hostfs_volume *vol)
{
    if (vol == NULL) {
        return;
    }
    fs_watch_destroy(vol->watch);
    vol->watch = NULL;
    vol->periodic_refresh = false;
    vol->synthetic_refresh = true;
    vol->watch_coverage_failed = false;
}

bool hostfs_test_inject_event(
    hostfs_volume *vol, uint32_t flags, const char *relative_path)
{
    hostfs_refresh_event *grown;
    int cap;

    if (vol == NULL || !vol->synthetic_refresh || flags == 0u ||
        !hostfs_refresh_path_valid(relative_path)) {
        return false;
    }
    if (vol->synthetic_event_count >= vol->synthetic_event_cap) {
        cap = vol->synthetic_event_cap < 16 ? 16 : vol->synthetic_event_cap * 2;
        grown = (hostfs_refresh_event *)realloc(
            vol->synthetic_events, (size_t)cap * sizeof(*grown));
        if (grown == NULL) {
            vol->refresh_rescan_required = true;
            return false;
        }
        vol->synthetic_events = grown;
        vol->synthetic_event_cap = cap;
    }
    vol->synthetic_events[vol->synthetic_event_count].flags = flags;
    snprintf(
        vol->synthetic_events[vol->synthetic_event_count].relative_path,
        sizeof(vol->synthetic_events[vol->synthetic_event_count].relative_path), "%s",
        relative_path);
    vol->synthetic_event_count++;
    return true;
}

void hostfs_test_require_full_rescan(hostfs_volume *vol)
{
    if (vol != NULL && vol->synthetic_refresh) {
        vol->refresh_rescan_required = true;
    }
}

void hostfs_test_reset_refresh_counters(hostfs_volume *vol)
{
    if (vol == NULL) {
        return;
    }
    vol->targeted_file_stats = 0;
    vol->targeted_directory_scans = 0;
    vol->full_rescans = 0;
}

int hostfs_test_targeted_file_stats(const hostfs_volume *vol)
{
    return vol != NULL ? vol->targeted_file_stats : 0;
}

int hostfs_test_targeted_directory_scans(const hostfs_volume *vol)
{
    return vol != NULL ? vol->targeted_directory_scans : 0;
}

int hostfs_test_full_rescans(const hostfs_volume *vol)
{
    return vol != NULL ? vol->full_rescans : 0;
}

bool hostfs_test_using_periodic_refresh(const hostfs_volume *vol)
{
    return vol != NULL && vol->periodic_refresh;
}

/* ---- node table, name arena, node-indexed marks ---- */

/* Grow the node table on demand: 16 slots, then doubling, capped at the
   architectural HOSTFS_MAX_NODES. Returns A2_OK if slot `need` exists after. */
static int hostfs_nodes_reserve(hostfs_volume *vol, int need)
{
    int cap = vol->node_cap;
    hostfs_node *grown;

    if (need <= cap) {
        return A2_OK;
    }
    if (need > HOSTFS_MAX_NODES) {
        return A2_ERR;
    }
    if (cap < 16) {
        cap = 16;
    }
    while (cap < need) {
        cap *= 2;
    }
    if (cap > HOSTFS_MAX_NODES) {
        cap = HOSTFS_MAX_NODES;
    }
    grown = (hostfs_node *)realloc(vol->nodes, (size_t)cap * sizeof(*grown));
    if (grown == NULL) {
        return A2_ERR;
    }
    memset(grown + vol->node_cap, 0, (size_t)(cap - vol->node_cap) * sizeof(*grown));
    vol->nodes = grown;
    vol->node_cap = cap;
    return A2_OK;
}

/* Append a host basename and return its arena offset. Offset 0 is the empty
   string, which is also what a zeroed node reads as, so 0 doubles as "unset". */
static uint32_t hostfs_names_add(hostfs_volume *vol, const char *name)
{
    size_t len;
    uint32_t off;

    if (name == NULL || name[0] == '\0') {
        return 0u;
    }
    len = strlen(name) + 1u;
    if (vol->names_len + len > vol->names_cap) {
        size_t cap = vol->names_cap < 1024u ? 1024u : vol->names_cap;
        char *grown;
        while (cap < vol->names_len + len) {
            cap *= 2u;
        }
        grown = (char *)realloc(vol->names, cap);
        if (grown == NULL) {
            return 0u;
        }
        vol->names = grown;
        vol->names_cap = cap;
    }
    if (vol->names_len == 0u) {
        /* Reserve offset 0 for the empty string. */
        vol->names[0] = '\0';
        vol->names_len = 1u;
        if (vol->names_len + len > vol->names_cap) {
            return 0u;
        }
    }
    off = (uint32_t)vol->names_len;
    memcpy(vol->names + vol->names_len, name, len);
    vol->names_len += len;
    return off;
}

static const char *hostfs_names_get(const hostfs_volume *vol, uint32_t off)
{
    if (vol->names == NULL || off >= vol->names_len) {
        return "";
    }
    return vol->names + off;
}

static const char *hostfs_node_basename(const hostfs_volume *vol, int index)
{
    if (index < 0 || index >= vol->node_count) {
        return "";
    }
    return hostfs_names_get(vol, vol->nodes[index].name_off);
}

static void hostfs_node_set_basename(hostfs_volume *vol, int index, const char *name)
{
    if (index < 0 || index >= vol->node_count) {
        return;
    }
    vol->nodes[index].name_off = hostfs_names_add(vol, name);
}

/* A bitmap indexed by node index. The node table can grow while one of these is
   live (the reconcile and rescan walks both add nodes as they go), so it grows
   with it rather than being sized once from the stack. */
typedef struct {
    uint8_t *bits;
    int cap;
} hostfs_marks;

static void hostfs_marks_init(hostfs_marks *m)
{
    m->bits = NULL;
    m->cap = 0;
}

static void hostfs_marks_free(hostfs_marks *m)
{
    free(m->bits);
    m->bits = NULL;
    m->cap = 0;
}

static void hostfs_marks_set(hostfs_marks *m, int index)
{
    if (index < 0) {
        return;
    }
    if (index >= m->cap) {
        int cap = m->cap < 64 ? 64 : m->cap;
        uint8_t *grown;
        while (cap <= index) {
            cap *= 2;
        }
        grown = (uint8_t *)realloc(m->bits, (size_t)cap);
        if (grown == NULL) {
            return;
        }
        memset(grown + m->cap, 0, (size_t)(cap - m->cap));
        m->bits = grown;
        m->cap = cap;
    }
    m->bits[index] = 1u;
}

static bool hostfs_marks_get(const hostfs_marks *m, int index)
{
    if (index < 0 || index >= m->cap) {
        return false;
    }
    return m->bits[index] != 0u;
}

static bool hostfs_int_list_contains(const int *items, int count, int value)
{
    int i;
    for (i = 0; i < count; ++i) {
        if (items[i] == value) {
            return true;
        }
    }
    return false;
}

static bool hostfs_int_list_add(
    int **items, int *count, int *cap, int value)
{
    int *grown;
    int new_cap;

    if (hostfs_int_list_contains(*items, *count, value)) {
        return true;
    }
    if (*count >= *cap) {
        new_cap = *cap < 8 ? 8 : *cap * 2;
        grown = (int *)realloc(*items, (size_t)new_cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        *items = grown;
        *cap = new_cap;
    }
    (*items)[(*count)++] = value;
    return true;
}

static void hostfs_schedule_directory_refresh(hostfs_volume *vol, int parent_index)
{

    if (vol == NULL) {
        return;
    }
    if (parent_index < 0) {
        vol->dirty_root = true;
        return;
    }
    if (!hostfs_int_list_add(
            &vol->dirty_parents, &vol->dirty_parent_count,
            &vol->dirty_parent_cap, parent_index)) {
        vol->internal_full_rescan_required = true;
    }
}

static uint64_t hostfs_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (uint64_t)time(NULL) * 1000ull;
    }
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000L);
#endif
}

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

static int hostfs_child_count(const hostfs_volume *vol, int parent_index)
{
    int i;
    int n = 0;
    if (vol == NULL) {
        return 0;
    }
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].parent_index == parent_index) {
            n++;
        }
    }
    return n;
}

int hostfs_file_count(const hostfs_volume *vol)
{
    int i;
    int n = 0;
    if (vol == NULL) {
        return 0;
    }
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active) {
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

/* Rebuild "<root>/<a>/<b>/<basename>" for a node by walking parent_index. The
   walk is bounded by HOSTFS_MAX_DEPTH and always runs immediately before an
   fopen/rename/stat that costs orders of magnitude more. */
static int hostfs_node_host_path(
    const hostfs_volume *vol, int index, char *out, size_t out_size)
{
    int chain[HOSTFS_MAX_DEPTH + 2];
    int depth = 0;
    int i;

    if (out == NULL || out_size == 0) {
        return A2_ERR;
    }
    out[0] = '\0';
    if (vol == NULL || index < 0 || index >= vol->node_count) {
        return A2_ERR;
    }
    while (index >= 0) {
        if (depth >= (int)(sizeof(chain) / sizeof(chain[0]))) {
            return A2_ERR; /* deeper than the scan can produce: cycle or corruption */
        }
        if (index >= vol->node_count) {
            return A2_ERR;
        }
        chain[depth++] = index;
        index = vol->nodes[index].parent_index;
    }
    snprintf(out, out_size, "%s", vol->root_path);
    for (i = depth - 1; i >= 0; --i) {
        char joined[HOSTFS_PATH_MAX];
        hostfs_path_join(joined, sizeof(joined), out, hostfs_node_basename(vol, chain[i]));
        snprintf(out, out_size, "%s", joined);
    }
    return A2_OK;
}

/* Host path of the directory that holds children of parent_index; the volume
   root for -1. Fills a caller buffer rather than returning a pointer, because
   the node table moves when it grows. */
static void hostfs_parent_host_path(
    const hostfs_volume *vol, int parent_index, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (parent_index < 0 || parent_index >= vol->node_count ||
        !vol->nodes[parent_index].active ||
        hostfs_node_host_path(vol, parent_index, out, out_size) != A2_OK) {
        snprintf(out, out_size, "%s", vol->root_path);
    }
}

static int hostfs_node_relative_path(
    const hostfs_volume *vol, int index, char *out, size_t out_size)
{
    int chain[HOSTFS_MAX_DEPTH + 2];
    int depth = 0;
    int i;

    if (vol == NULL || out == NULL || out_size == 0u || index < 0 ||
        index >= vol->node_count) {
        return A2_ERR;
    }
    while (index >= 0) {
        if (depth >= (int)(sizeof(chain) / sizeof(chain[0])) ||
            index >= vol->node_count) {
            return A2_ERR;
        }
        chain[depth++] = index;
        index = vol->nodes[index].parent_index;
    }
    out[0] = '\0';
    for (i = depth - 1; i >= 0; --i) {
        const char *basename = hostfs_node_basename(vol, chain[i]);
        size_t used = strlen(out);
        size_t len = strlen(basename);
        if (used + (used > 0u ? 1u : 0u) + len >= out_size) {
            return A2_ERR;
        }
        if (used > 0u) {
            out[used++] = '/';
            out[used] = '\0';
        }
        memcpy(out + used, basename, len + 1u);
    }
    return A2_OK;
}

/* Add the directory watch before its readdir. The recursive backends treat
   this as an idempotent health check; Linux installs the per-directory watch. */
static void hostfs_watch_scanned_directory(hostfs_volume *vol, int parent_index)
{
    char relative_path[FS_WATCH_PATH_MAX];

    if (vol == NULL || vol->watch == NULL) {
        return;
    }
    if (parent_index < 0) {
        relative_path[0] = '\0';
    } else if (hostfs_node_relative_path(
                   vol, parent_index, relative_path, sizeof(relative_path)) != A2_OK) {
        vol->watch_coverage_failed = true;
        return;
    }
    if (!fs_watch_add_directory(vol->watch, relative_path)) {
        vol->watch_coverage_failed = true;
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
    int idx;
    if (vol->block_to_map == NULL) {
        return -1;
    }
    idx = (int)vol->block_to_map[block];
    assert(idx >= -1 && idx < vol->map_count);
    assert(idx < 0 || vol->map[idx].block == block);
    return idx;
}

/* Validate both directions without turning each insertion into an O(map) walk.
   Removed entries are checked locally by hostfs_map_remove_at; this catches any
   surviving map slot whose direct index drifted. */
static void hostfs_map_assert_consistent(const hostfs_volume *vol)
{
#ifndef NDEBUG
    int block;
    int i;
    assert(vol->block_to_map != NULL);
    for (i = 0; i < vol->map_count; ++i) {
        assert(vol->block_to_map[vol->map[i].block] == i);
    }
    for (block = 0; block <= HOSTFS_TOTAL_BLOCKS; ++block) {
        int idx = (int)vol->block_to_map[block];
        assert(idx >= -1 && idx < vol->map_count);
        assert(idx < 0 || vol->map[idx].block == (uint16_t)block);
    }
#else
    (void)vol;
#endif
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
    ref = &vol->map[vol->map_count];
    ref->block = block;
    ref->kind = HOSTFS_MAP_RAM;
    ref->u.ram = ram;
    vol->block_to_map[block] = vol->map_count;
    vol->map_count++;
    assert(hostfs_map_find(vol, block) == vol->map_count - 1);
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
    ref = &vol->map[vol->map_count];
    ref->block = block;
    ref->kind = HOSTFS_MAP_HOST;
    ref->u.host.file_index = file_index;
    ref->u.host.offset = offset;
    vol->block_to_map[block] = vol->map_count;
    vol->map_count++;
    assert(hostfs_map_find(vol, block) == vol->map_count - 1);
    return A2_OK;
}

static void hostfs_map_remove_at(hostfs_volume *vol, int idx)
{
    int last;
    uint16_t removed_block;

    assert(idx >= 0 && idx < vol->map_count);
    removed_block = vol->map[idx].block;
    if (vol->map[idx].kind == HOSTFS_MAP_RAM) {
        free(vol->map[idx].u.ram);
    }

    last = vol->map_count - 1;
    vol->block_to_map[removed_block] = -1;
    if (idx != last) {
        vol->map[idx] = vol->map[last];
        vol->block_to_map[vol->map[idx].block] = idx;
    }
    vol->map_count = last;

    assert(vol->block_to_map[removed_block] == -1);
    assert(idx == last || hostfs_map_find(vol, vol->map[idx].block) == idx);
}

static void hostfs_map_remove_block(hostfs_volume *vol, uint16_t block)
{
    int idx = hostfs_map_find(vol, block);
    if (idx < 0) {
        return;
    }
    hostfs_map_remove_at(vol, idx);
}

static void hostfs_map_remove_file(hostfs_volume *vol, int file_index)
{
    int i = 0;
    while (i < vol->map_count) {
        if (vol->map[i].kind == HOSTFS_MAP_HOST &&
            vol->map[i].u.host.file_index == file_index) {
            hostfs_bitmap_mark_free(vol->bitmap, vol->map[i].block);
            hostfs_map_remove_at(vol, i);
            continue;
        }
        ++i;
    }
}

static void hostfs_map_remove_dir_blocks(hostfs_volume *vol, int dir_index)
{
    uint16_t block;
    uint16_t guard = 0;
    if (dir_index < 0 || dir_index >= vol->node_count ||
        vol->nodes[dir_index].kind != HOSTFS_KIND_DIR) {
        return;
    }
    block = vol->nodes[dir_index].key_block;
    while (block != 0u && guard++ < 1024u) {
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        uint16_t next = 0;
        if (dir != NULL) {
            next = hostfs_read_u16(dir + 2);
        }
        hostfs_bitmap_mark_free(vol->bitmap, block);
        hostfs_map_remove_block(vol, block);
        block = next;
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

static int hostfs_active_name_exists(
    const hostfs_volume *vol, int parent_index, const char *name, int skip)
{
    int i;
    for (i = 0; i < vol->node_count; ++i) {
        if (i == skip || !vol->nodes[i].active) {
            continue;
        }
        if (vol->nodes[i].parent_index == parent_index &&
            strcmp(vol->nodes[i].prodos_name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hostfs_find_by_key(const hostfs_volume *vol, uint16_t key)
{
    int i;
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].key_block == key) {
            return i;
        }
    }
    return -1;
}

static int hostfs_find_by_name(const hostfs_volume *vol, int parent_index, const char *name)
{
    int i;
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].parent_index == parent_index &&
            strcmp(vol->nodes[i].prodos_name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Identify a node by where it lives on the host: parent plus host basename.
   Comparing full paths would mean rebuilding one per candidate, which is
   quadratic; the basename is one arena lookup. */
static int hostfs_find_by_basename(
    const hostfs_volume *vol, int parent_index, const char *basename)
{
    int i;
    if (basename == NULL || basename[0] == '\0') {
        return -1;
    }
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].parent_index == parent_index &&
            strcmp(hostfs_node_basename(vol, i), basename) == 0) {
            return i;
        }
    }
    return -1;
}

static int hostfs_find_by_relative_path(
    const hostfs_volume *vol, const char *relative_path)
{
    char copy[FS_WATCH_PATH_MAX];
    char *part;
    int parent_index = -1;

    if (vol == NULL || !hostfs_refresh_path_valid(relative_path) ||
        relative_path[0] == '\0') {
        return -1;
    }
    snprintf(copy, sizeof(copy), "%s", relative_path);
    part = copy;
    while (part != NULL && part[0] != '\0') {
        char *slash = strchr(part, '/');
        int index;
        if (slash != NULL) {
            *slash = '\0';
        }
        index = hostfs_find_by_basename(vol, parent_index, part);
        if (index < 0) {
            return -1;
        }
        parent_index = index;
        part = slash != NULL ? slash + 1 : NULL;
    }
    return parent_index;
}

static bool hostfs_parent_from_relative_path(
    const hostfs_volume *vol, const char *relative_path, int *out_parent)
{
    char parent_path[FS_WATCH_PATH_MAX];
    char *slash;
    int index;

    if (out_parent == NULL || !hostfs_refresh_path_valid(relative_path) ||
        relative_path[0] == '\0') {
        return false;
    }
    snprintf(parent_path, sizeof(parent_path), "%s", relative_path);
    slash = strrchr(parent_path, '/');
    if (slash == NULL) {
        *out_parent = -1;
        return true;
    }
    *slash = '\0';
    index = hostfs_find_by_relative_path(vol, parent_path);
    if (index < 0 || !vol->nodes[index].active ||
        vol->nodes[index].kind != HOSTFS_KIND_DIR) {
        return false;
    }
    *out_parent = index;
    return true;
}

static int hostfs_parent_depth(const hostfs_volume *vol, int parent_index)
{
    int depth = 0;
    while (parent_index >= 0 && depth <= HOSTFS_MAX_DEPTH + 1) {
        if (parent_index >= vol->node_count) {
            return HOSTFS_MAX_DEPTH + 1;
        }
        depth++;
        parent_index = vol->nodes[parent_index].parent_index;
    }
    return depth;
}

static uint16_t hostfs_parent_key(const hostfs_volume *vol, int parent_index)
{
    if (parent_index < 0) {
        return 2u;
    }
    if (parent_index >= vol->node_count || !vol->nodes[parent_index].active ||
        vol->nodes[parent_index].kind != HOSTFS_KIND_DIR) {
        return 2u;
    }
    return vol->nodes[parent_index].key_block;
}

static int hostfs_alloc_file_slot(hostfs_volume *vol)
{
    int i;
    for (i = 0; i < vol->node_count; ++i) {
        if (!vol->nodes[i].active) {
            return i;
        }
    }
    if (vol->node_count >= HOSTFS_MAX_NODES ||
        hostfs_nodes_reserve(vol, vol->node_count + 1) != A2_OK) {
        if (!vol->node_limit_warned) {
            hostfs_warn(
                vol, "%s: node limit %d reached; remaining entries ignored",
                vol->root_path, HOSTFS_MAX_NODES);
            vol->node_limit_warned = true;
        }
        return -1;
    }
    return vol->node_count++;
}

/* ---- host file I/O ---- */

static int hostfs_host_write(
    hostfs_volume *vol, int file_index, uint32_t offset, const uint8_t *data, size_t len)
{
    FILE *fp;
    hostfs_node *file;
    long want_end;
    char path[HOSTFS_PATH_MAX];

    if (file_index < 0 || file_index >= vol->node_count || !vol->nodes[file_index].active) {
        return A2_ERR;
    }
    if (hostfs_node_host_path(vol, file_index, path, sizeof(path)) != A2_OK) {
        return A2_ERR;
    }
    file = &vol->nodes[file_index];
    fp = fopen(path, "rb+");
    if (fp == NULL) {
        fp = fopen(path, "wb+");
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
        file->host_size = (uint32_t)want_end;
    }
    vol->dirty = true;
    return A2_OK;
}

static int hostfs_host_truncate(hostfs_volume *vol, int file_index, uint32_t eof)
{
    FILE *fp;
    hostfs_node *file;
    char path[HOSTFS_PATH_MAX];
    if (file_index < 0 || file_index >= vol->node_count || !vol->nodes[file_index].active) {
        return A2_ERR;
    }
    if (hostfs_node_host_path(vol, file_index, path, sizeof(path)) != A2_OK) {
        return A2_ERR;
    }
    file = &vol->nodes[file_index];
    fp = fopen(path, "rb+");
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

static int hostfs_find_host_by_stem_in(
    const char *parent_host_path, const char *stem, char *out_path, size_t out_size)
{
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search[HOSTFS_PATH_MAX];
    hostfs_path_join(search, sizeof(search), parent_host_path, "*");
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
            hostfs_path_join(out_path, out_size, parent_host_path, data.cFileName);
            FindClose(handle);
            return A2_OK;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return A2_ERR;
#else
    DIR *dir = opendir(parent_host_path);
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
            hostfs_path_join(out_path, out_size, parent_host_path, entry->d_name);
            closedir(dir);
            return A2_OK;
        }
    }
    closedir(dir);
    return A2_ERR;
#endif
}

static int hostfs_find_host_dir_by_name(
    const char *parent_host_path, const char *prodos_name, char *out_path, size_t out_size)
{
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search[HOSTFS_PATH_MAX];
    hostfs_path_join(search, sizeof(search), parent_host_path, "*");
    handle = FindFirstFileA(search, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return A2_ERR;
    }
    do {
        char mangled[HOSTFS_NAME_MAX];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        if (hostfs_should_skip_basename(data.cFileName)) {
            continue;
        }
        if (!hostfs_mangle_prodos_name(data.cFileName, mangled, sizeof(mangled))) {
            continue;
        }
        if (strcmp(mangled, prodos_name) == 0) {
            hostfs_path_join(out_path, out_size, parent_host_path, data.cFileName);
            FindClose(handle);
            return A2_OK;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return A2_ERR;
#else
    DIR *dir = opendir(parent_host_path);
    struct dirent *entry;
    if (dir == NULL) {
        return A2_ERR;
    }
    while ((entry = readdir(dir)) != NULL) {
        char mangled[HOSTFS_NAME_MAX];
        char full[HOSTFS_PATH_MAX];
        struct stat st;
        if (hostfs_should_skip_basename(entry->d_name)) {
            continue;
        }
        hostfs_path_join(full, sizeof(full), parent_host_path, entry->d_name);
        if (stat(full, &st) != 0 || !A2M_STAT_ISDIR(st.st_mode)) {
            continue;
        }
        if (!hostfs_mangle_prodos_name(entry->d_name, mangled, sizeof(mangled))) {
            continue;
        }
        if (strcmp(mangled, prodos_name) == 0) {
            snprintf(out_path, out_size, "%s", full);
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
    int parent_index,
    const char *prodos_name,
    uint8_t file_type,
    uint16_t aux_type,
    char *out_path,
    size_t out_size)
{
    char naps[HOSTFS_PATH_MAX];
    char existing[HOSTFS_PATH_MAX];
    char parent_path[HOSTFS_PATH_MAX];

    hostfs_parent_host_path(vol, parent_index, parent_path, sizeof(parent_path));
    if (!hostfs_compose_naps_filename(prodos_name, file_type, aux_type, naps, sizeof(naps))) {
        return A2_ERR;
    }
    if (hostfs_find_host_by_stem_in(parent_path, prodos_name, existing, sizeof(existing)) ==
        A2_OK) {
        const char *base = hostfs_path_basename(existing);
        if (strcmp(base, naps) != 0) {
            char dest[HOSTFS_PATH_MAX];
            hostfs_path_join(dest, sizeof(dest), parent_path, naps);
            if (rename(existing, dest) == 0) {
                snprintf(out_path, out_size, "%s", dest);
            } else {
                snprintf(out_path, out_size, "%s", existing);
            }
        } else {
            snprintf(out_path, out_size, "%s", existing);
        }
        return A2_OK;
    }
    hostfs_path_join(out_path, out_size, parent_path, naps);
    {
        FILE *fp = fopen(out_path, "wb");
        if (fp == NULL) {
            return A2_ERR;
        }
        fclose(fp);
    }
    return A2_OK;
}

static int hostfs_create_or_reuse_host_dir(
    hostfs_volume *vol,
    int parent_index,
    const char *prodos_name,
    char *out_path,
    size_t out_size)
{
    char parent_path[HOSTFS_PATH_MAX];
    char existing[HOSTFS_PATH_MAX];

    hostfs_parent_host_path(vol, parent_index, parent_path, sizeof(parent_path));
    if (hostfs_find_host_dir_by_name(parent_path, prodos_name, existing, sizeof(existing)) ==
        A2_OK) {
        snprintf(out_path, out_size, "%s", existing);
        return A2_OK;
    }
    hostfs_path_join(out_path, out_size, parent_path, prodos_name);
    if (hostfs_mkdir(out_path) != 0) {
        struct stat st;
        if (stat(out_path, &st) != 0 || !A2M_STAT_ISDIR(st.st_mode)) {
            return A2_ERR;
        }
    }
    return A2_OK;
}

/* ---- storage allocation ---- */

static int hostfs_alloc_file_storage(hostfs_volume *vol, int file_index)
{
    hostfs_node *file = &vol->nodes[file_index];
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

static void hostfs_fill_file_entry(uint8_t *entry, const hostfs_node *file, uint16_t header_ptr)
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

static int hostfs_patch_dir_file_count(hostfs_volume *vol, int parent_index)
{
    uint16_t key = hostfs_parent_key(vol, parent_index);
    uint8_t *dir = hostfs_map_ram_ptr(vol, key);
    if (dir == NULL) {
        return A2_ERR;
    }
    hostfs_write_u16(dir + 4 + 0x21, (uint16_t)hostfs_child_count(vol, parent_index));
    return A2_OK;
}

static int hostfs_patch_volume_file_count(hostfs_volume *vol)
{
    return hostfs_patch_dir_file_count(vol, -1);
}

static uint16_t hostfs_dir_block_at(const hostfs_volume *vol, int dir_index, uint16_t which)
{
    uint16_t block;
    uint16_t i;
    if (dir_index < 0) {
        if (which >= vol->dir_block_count) {
            return 0;
        }
        return (uint16_t)(2u + which);
    }
    if (dir_index >= vol->node_count || !vol->nodes[dir_index].active ||
        vol->nodes[dir_index].kind != HOSTFS_KIND_DIR) {
        return 0;
    }
    if (which >= vol->nodes[dir_index].dir_block_count) {
        return 0;
    }
    block = vol->nodes[dir_index].key_block;
    for (i = 0; i < which; ++i) {
        const uint8_t *dir = NULL;
        int idx = hostfs_map_find(vol, block);
        if (idx < 0 || vol->map[idx].kind != HOSTFS_MAP_RAM || vol->map[idx].u.ram == NULL) {
            return 0;
        }
        dir = vol->map[idx].u.ram;
        block = hostfs_read_u16(dir + 2);
        if (block == 0u) {
            return 0;
        }
    }
    return block;
}

static uint16_t hostfs_dir_block_count_of(const hostfs_volume *vol, int dir_index)
{
    if (dir_index < 0) {
        return vol->dir_block_count;
    }
    if (dir_index >= vol->node_count || !vol->nodes[dir_index].active ||
        vol->nodes[dir_index].kind != HOSTFS_KIND_DIR) {
        return 0;
    }
    return vol->nodes[dir_index].dir_block_count;
}

static uint8_t *hostfs_find_dir_entry_slot(
    hostfs_volume *vol, int parent_index, const char *name, int *out_free)
{
    uint16_t d;
    uint16_t count = hostfs_dir_block_count_of(vol, parent_index);
    if (out_free != NULL) {
        *out_free = 0;
    }
    for (d = 0; d < count; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL || block == 0u) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t nl = e[0] & 0x0Fu;
            char nm[16];
            if (e[0] == 0) {
                if (out_free != NULL && *out_free == 0) {
                    *out_free = 1;
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

static uint8_t *hostfs_first_free_dir_entry(hostfs_volume *vol, int parent_index)
{
    uint16_t d;
    uint16_t count = hostfs_dir_block_count_of(vol, parent_index);
    for (d = 0; d < count; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL || block == 0u) {
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

static void hostfs_fill_subdir_header(
    uint8_t *hdr,
    const hostfs_node *dir_node,
    uint16_t parent_entry_block,
    uint8_t parent_entry_number,
    uint16_t active_count)
{
    memset(hdr, 0, HOSTFS_ENTRY_LENGTH);
    hdr[0] = (uint8_t)((HOSTFS_STOR_SUBDIR_HDR << 4) | (dir_node->name_len & 0x0Fu));
    memcpy(hdr + 1, dir_node->prodos_name, dir_node->name_len);
    hdr[0x10] = 0x75u; /* ProDOS 8 subdirectory header marker */
    hdr[0x1E] = HOSTFS_ACCESS_DIR;
    hdr[0x1F] = HOSTFS_ENTRY_LENGTH;
    hdr[0x20] = HOSTFS_ENTRIES_PER_BLOCK;
    hostfs_write_u16(hdr + 0x21, active_count);
    hostfs_write_u16(hdr + 0x23, parent_entry_block);
    hdr[0x25] = parent_entry_number;
    hdr[0x26] = HOSTFS_ENTRY_LENGTH;
}

/* Record where a child entry lives in its parent directory blocks. */
static void hostfs_note_parent_entry(
    hostfs_volume *vol, int child_index, uint16_t block, int slot)
{
    if (child_index < 0 || child_index >= vol->node_count) {
        return;
    }
    vol->nodes[child_index].parent_entry_block = block;
    vol->nodes[child_index].parent_entry_number = (uint8_t)(slot + 1); /* 1-based */
}

static int hostfs_fill_dir_children(
    hostfs_volume *vol, int parent_index, uint16_t header_ptr, uint16_t block_count)
{
    int child = 0;
    uint16_t d;

    for (d = 0; d < block_count; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL || block == 0u) {
            return A2_ERR;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            while (child < vol->node_count &&
                   !(vol->nodes[child].active && vol->nodes[child].parent_index == parent_index)) {
                child++;
            }
            if (child >= vol->node_count) {
                return A2_OK;
            }
            hostfs_fill_file_entry(
                dir + 4 + slot * HOSTFS_ENTRY_LENGTH, &vol->nodes[child], header_ptr);
            hostfs_note_parent_entry(vol, child, block, slot);
            child++;
        }
    }
    return A2_OK;
}

static int hostfs_build_volume_directory(hostfs_volume *vol)
{
    uint16_t d;

    for (d = 0; d < vol->dir_block_count; ++d) {
        uint16_t block = (uint16_t)(2u + d);
        uint8_t *dir;
        uint16_t prev = d == 0u ? 0u : (uint16_t)(block - 1u);
        uint16_t next =
            (d + 1u < vol->dir_block_count) ? (uint16_t)(block + 1u) : 0u;

        if (hostfs_map_add_ram(vol, block, NULL) != A2_OK) {
            return A2_ERR;
        }
        dir = hostfs_map_ram_ptr(vol, block);
        hostfs_write_u16(dir + 0, prev);
        hostfs_write_u16(dir + 2, next);

        if (d == 0u) {
            uint8_t *hdr = dir + 4;
            hdr[0] = (uint8_t)((HOSTFS_STOR_VOL_HDR << 4) | (vol->volume_name_len & 0x0Fu));
            memcpy(hdr + 1, vol->volume_name, vol->volume_name_len);
            hdr[0x1C] = 0;
            hdr[0x1D] = 0;
            hdr[0x1E] = HOSTFS_ACCESS_VOL;
            hdr[0x1F] = HOSTFS_ENTRY_LENGTH;
            hdr[0x20] = HOSTFS_ENTRIES_PER_BLOCK;
            hostfs_write_u16(hdr + 0x21, (uint16_t)hostfs_child_count(vol, -1));
            hostfs_write_u16(hdr + 0x23, vol->bitmap_block);
            hostfs_write_u16(hdr + 0x25, vol->total_blocks);
        }
    }
    return hostfs_fill_dir_children(vol, -1, 2u, vol->dir_block_count);
}

static int hostfs_alloc_dir_storage(hostfs_volume *vol, int dir_index)
{
    hostfs_node *dir_node;
    uint16_t n;
    uint16_t i;
    uint16_t key;

    if (dir_index < 0 || dir_index >= vol->node_count) {
        return A2_ERR;
    }
    dir_node = &vol->nodes[dir_index];
    n = dir_node->dir_block_count;
    if (n == 0u) {
        n = 1u;
        dir_node->dir_block_count = 1u;
    }
    key = hostfs_alloc_block(vol);
    if (key == 0u || key >= vol->total_blocks) {
        return A2_ERR;
    }
    dir_node->key_block = key;
    dir_node->storage_type = HOSTFS_STOR_SUBDIR;
    dir_node->file_type = HOSTFS_FILE_TYPE_DIR;
    dir_node->aux_type = 0;
    dir_node->blocks_used = n;
    dir_node->eof = (uint32_t)n * HOSTFS_BLOCK_SIZE;
    if (hostfs_map_add_ram(vol, key, NULL) != A2_OK) {
        return A2_ERR;
    }
    hostfs_bitmap_mark_used(vol->bitmap, key);
    for (i = 1; i < n; ++i) {
        uint16_t b = hostfs_alloc_block(vol);
        if (b == 0u || b >= vol->total_blocks) {
            return A2_ERR;
        }
        if (hostfs_map_add_ram(vol, b, NULL) != A2_OK) {
            return A2_ERR;
        }
        hostfs_bitmap_mark_used(vol->bitmap, b);
    }
    return A2_OK;
}

static int hostfs_build_subdirectory(hostfs_volume *vol, int dir_index)
{
    hostfs_node *dir_node;
    uint16_t d;
    uint16_t n;
    uint16_t active;
    uint16_t *blocks;

    if (dir_index < 0 || dir_index >= vol->node_count || !vol->nodes[dir_index].active ||
        vol->nodes[dir_index].kind != HOSTFS_KIND_DIR) {
        return A2_ERR;
    }
    dir_node = &vol->nodes[dir_index];
    n = dir_node->dir_block_count;
    active = (uint16_t)hostfs_child_count(vol, dir_index);
    blocks = (uint16_t *)calloc(n, sizeof(uint16_t));
    if (blocks == NULL) {
        return A2_ERR;
    }
    /* Collect the RAM dir blocks that were just allocated for this key.
       At mount they are sequential from key_block; after growth, walk links. */
    blocks[0] = dir_node->key_block;
    for (d = 1; d < n; ++d) {
        uint8_t *prev = hostfs_map_ram_ptr(vol, blocks[d - 1u]);
        if (prev != NULL && hostfs_read_u16(prev + 2) != 0u) {
            blocks[d] = hostfs_read_u16(prev + 2);
        } else {
            blocks[d] = (uint16_t)(dir_node->key_block + d);
        }
    }

    for (d = 0; d < n; ++d) {
        uint16_t block = blocks[d];
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        uint16_t prev = d == 0u ? 0u : blocks[d - 1u];
        uint16_t next = (d + 1u < n) ? blocks[d + 1u] : 0u;
        if (dir == NULL) {
            free(blocks);
            return A2_ERR;
        }
        memset(dir, 0, HOSTFS_BLOCK_SIZE);
        hostfs_write_u16(dir + 0, prev);
        hostfs_write_u16(dir + 2, next);
        if (d == 0u) {
            hostfs_fill_subdir_header(
                dir + 4, dir_node, dir_node->parent_entry_block, dir_node->parent_entry_number,
                active);
        }
    }
    free(blocks);
    return hostfs_fill_dir_children(vol, dir_index, dir_node->key_block, n);
}

static int hostfs_build_all_directories(hostfs_volume *vol)
{
    int i;
    if (hostfs_build_volume_directory(vol) != A2_OK) {
        return A2_ERR;
    }
    /* First pass notes parent_entry_* for every root child including dirs. */
    for (i = 0; i < vol->node_count; ++i) {
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_DIR) {
            continue;
        }
        if (hostfs_build_subdirectory(vol, i) != A2_OK) {
            return A2_ERR;
        }
    }
    /* Second pass: subdir headers now have correct parent_entry from volume/parent fill. */
    for (i = 0; i < vol->node_count; ++i) {
        hostfs_node *dir_node;
        uint8_t *dir;
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_DIR) {
            continue;
        }
        dir_node = &vol->nodes[i];
        dir = hostfs_map_ram_ptr(vol, dir_node->key_block);
        if (dir == NULL) {
            return A2_ERR;
        }
        hostfs_fill_subdir_header(
            dir + 4, dir_node, dir_node->parent_entry_block, dir_node->parent_entry_number,
            (uint16_t)hostfs_child_count(vol, i));
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
    hostfs_kind kind;
    char prodos[HOSTFS_NAME_MAX];
    uint8_t type;
    uint16_t aux;
    char host_path[HOSTFS_PATH_MAX];
    char basename[HOSTFS_BASENAME_MAX];
    uint64_t size;
    time_t mtime;
} hostfs_scan_ent;

static int hostfs_scan_array_reserve(hostfs_scan_ent **items, int *cap, int need)
{
    int grown_cap = *cap;
    hostfs_scan_ent *grown;

    if (need <= grown_cap) {
        return A2_OK;
    }
    if (grown_cap < 16) {
        grown_cap = 16;
    }
    while (grown_cap < need) {
        if (grown_cap > INT_MAX / 2) {
            grown_cap = need;
            break;
        }
        grown_cap *= 2;
    }
    grown = (hostfs_scan_ent *)realloc(*items, (size_t)grown_cap * sizeof(*grown));
    if (grown == NULL) {
        return A2_ERR;
    }
    *items = grown;
    *cap = grown_cap;
    return A2_OK;
}

static int hostfs_basename_array_reserve(
    char (**items)[HOSTFS_BASENAME_MAX], int *cap, int need)
{
    int grown_cap = *cap;
    char (*grown)[HOSTFS_BASENAME_MAX];

    if (need <= grown_cap) {
        return A2_OK;
    }
    if (grown_cap < 16) {
        grown_cap = 16;
    }
    while (grown_cap < need) {
        if (grown_cap > INT_MAX / 2) {
            grown_cap = need;
            break;
        }
        grown_cap *= 2;
    }
    grown = (char (*)[HOSTFS_BASENAME_MAX])realloc(
        *items, (size_t)grown_cap * sizeof(*grown));
    if (grown == NULL) {
        return A2_ERR;
    }
    *items = grown;
    *cap = grown_cap;
    return A2_OK;
}

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

static bool hostfs_should_skip_basename(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return true;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return true;
    }
    if (hostfs_basename_cmp(name, HOSTFS_ORDER_FILENAME) == 0) {
        return true;
    }
    if (name[0] == '.') {
        return true;
    }
    return false;
}

/* Collect every mountable immediate child, growing with the directory rather
   than imposing an implementation cap on its catalog. */
static int hostfs_collect_scans_in(
    const char *host_dir_path, hostfs_scan_ent **out_scans, int *out_count)
{
    hostfs_scan_ent *out = NULL;
    int count = 0;
    int cap = 0;
    *out_scans = NULL;
    *out_count = 0;
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search[HOSTFS_PATH_MAX];
    hostfs_path_join(search, sizeof(search), host_dir_path, "*");
    handle = FindFirstFileA(search, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return A2_ERR;
    }
    do {
        struct stat st;
        char full[HOSTFS_PATH_MAX];
        if (hostfs_should_skip_basename(data.cFileName)) {
            continue;
        }
        if (hostfs_scan_array_reserve(&out, &cap, count + 1) != A2_OK) {
            FindClose(handle);
            free(out);
            return A2_ERR;
        }
        hostfs_path_join(full, sizeof(full), host_dir_path, data.cFileName);
        if (stat(full, &st) != 0) {
            continue;
        }
        if (A2M_STAT_ISDIR(st.st_mode) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!hostfs_mangle_prodos_name(
                    data.cFileName, out[count].prodos, sizeof(out[count].prodos))) {
                continue;
            }
            out[count].kind = HOSTFS_KIND_DIR;
            out[count].type = HOSTFS_FILE_TYPE_DIR;
            out[count].aux = 0;
            out[count].size = 0;
            out[count].mtime = st.st_mtime;
            snprintf(out[count].host_path, sizeof(out[count].host_path), "%s", full);
            snprintf(out[count].basename, sizeof(out[count].basename), "%s", data.cFileName);
            count++;
            continue;
        }
        if (!hostfs_naps_parse_name(
                data.cFileName, out[count].prodos, sizeof(out[count].prodos),
                &out[count].type, &out[count].aux)) {
            continue;
        }
        out[count].kind = HOSTFS_KIND_FILE;
        snprintf(out[count].host_path, sizeof(out[count].host_path), "%s", full);
        snprintf(out[count].basename, sizeof(out[count].basename), "%s", data.cFileName);
        out[count].size = st.st_size < 0 ? 0u : (uint64_t)st.st_size;
        out[count].mtime = st.st_mtime;
        count++;
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
#else
    DIR *dir = opendir(host_dir_path);
    struct dirent *entry;
    if (dir == NULL) {
        return A2_ERR;
    }
    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        char full[HOSTFS_PATH_MAX];
        if (hostfs_should_skip_basename(entry->d_name)) {
            continue;
        }
        if (hostfs_scan_array_reserve(&out, &cap, count + 1) != A2_OK) {
            closedir(dir);
            free(out);
            return A2_ERR;
        }
        hostfs_path_join(full, sizeof(full), host_dir_path, entry->d_name);
        if (stat(full, &st) != 0) {
            continue;
        }
        if (A2M_STAT_ISDIR(st.st_mode)) {
            if (!hostfs_mangle_prodos_name(
                    entry->d_name, out[count].prodos, sizeof(out[count].prodos))) {
                continue;
            }
            out[count].kind = HOSTFS_KIND_DIR;
            out[count].type = HOSTFS_FILE_TYPE_DIR;
            out[count].aux = 0;
            out[count].size = 0;
            out[count].mtime = st.st_mtime;
            snprintf(out[count].host_path, sizeof(out[count].host_path), "%s", full);
            snprintf(out[count].basename, sizeof(out[count].basename), "%s", entry->d_name);
            count++;
            continue;
        }
        if (!hostfs_naps_parse_name(
                entry->d_name, out[count].prodos, sizeof(out[count].prodos),
                &out[count].type, &out[count].aux)) {
            continue;
        }
        out[count].kind = HOSTFS_KIND_FILE;
        snprintf(out[count].host_path, sizeof(out[count].host_path), "%s", full);
        snprintf(out[count].basename, sizeof(out[count].basename), "%s", entry->d_name);
        out[count].size = st.st_size < 0 ? 0u : (uint64_t)st.st_size;
        out[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dir);
#endif
    *out_scans = out;
    *out_count = count;
    return A2_OK;
}

static int hostfs_load_order_file(
    const char *root_path, char (**out_names)[HOSTFS_BASENAME_MAX], int *out_count)
{
    char path[HOSTFS_PATH_MAX];
    char (*names)[HOSTFS_BASENAME_MAX] = NULL;
    FILE *fp;
    char line[HOSTFS_BASENAME_MAX + 32];
    int count = 0;
    int cap = 0;

    *out_names = NULL;
    *out_count = 0;

    hostfs_path_join(path, sizeof(path), root_path, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return A2_OK;
    }
    while (fgets(line, (int)sizeof(line), fp) != NULL) {
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
        /* NAPS file basenames or plain mangled directory names. */
        {
            char prodos[HOSTFS_NAME_MAX];
            uint8_t type;
            uint16_t aux;
            if (!hostfs_naps_parse_name(p, prodos, sizeof(prodos), &type, &aux) &&
                !hostfs_mangle_prodos_name(p, prodos, sizeof(prodos))) {
                continue;
            }
        }
        if (hostfs_basename_array_reserve(&names, &cap, count + 1) != A2_OK) {
            fclose(fp);
            free(names);
            return A2_ERR;
        }
        snprintf(names[count], HOSTFS_BASENAME_MAX, "%s", p);
        count++;
    }
    fclose(fp);
    *out_names = names;
    *out_count = count;
    return A2_OK;
}

static int hostfs_apply_order_to_scans(
    hostfs_scan_ent *scans, int n, const char names[][HOSTFS_BASENAME_MAX], int name_count)
{
    hostfs_scan_ent *ordered;
    hostfs_scan_ent *rest;
    bool *used;
    int out = 0;
    int rest_n = 0;
    int i;
    int k;

    if (n <= 0) {
        return A2_OK;
    }
    ordered = (hostfs_scan_ent *)malloc((size_t)n * sizeof(hostfs_scan_ent));
    rest = (hostfs_scan_ent *)malloc((size_t)n * sizeof(hostfs_scan_ent));
    used = (bool *)calloc((size_t)n, sizeof(*used));
    if (ordered == NULL || rest == NULL || used == NULL) {
        free(ordered);
        free(rest);
        free(used);
        return A2_ERR;
    }

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
    free(used);
    return A2_OK;
}

static int hostfs_catalog_basenames_in(
    hostfs_volume *vol,
    int parent_index,
    char (**out_names)[HOSTFS_BASENAME_MAX],
    int *out_count)
{
    char (*names)[HOSTFS_BASENAME_MAX] = NULL;
    int count = 0;
    int cap = 0;
    uint16_t d;
    uint16_t blocks = hostfs_dir_block_count_of(vol, parent_index);

    *out_names = NULL;
    *out_count = 0;
    for (d = 0; d < blocks; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL || block == 0u) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t st;
            uint8_t nl;
            char nm[HOSTFS_NAME_MAX];
            int fi;
            if (e[0] == 0) {
                continue;
            }
            st = (uint8_t)(e[0] >> 4);
            if (!((st >= HOSTFS_STOR_SEEDLING && st <= HOSTFS_STOR_TREE) ||
                  st == HOSTFS_STOR_SUBDIR)) {
                continue;
            }
            nl = (uint8_t)(e[0] & 0x0Fu);
            memcpy(nm, e + 1, nl);
            nm[nl] = '\0';
            fi = hostfs_find_by_name(vol, parent_index, nm);
            if (fi < 0) {
                uint16_t key = hostfs_read_u16(e + 0x11);
                fi = hostfs_find_by_key(vol, key);
            }
            if (fi < 0 || !vol->nodes[fi].active) {
                continue;
            }
            if (hostfs_basename_array_reserve(&names, &cap, count + 1) != A2_OK) {
                free(names);
                return A2_ERR;
            }
            snprintf(
                names[count], HOSTFS_BASENAME_MAX, "%s", hostfs_node_basename(vol, fi));
            count++;
        }
    }
    *out_names = names;
    *out_count = count;
    return A2_OK;
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

static int hostfs_persist_order_manifest_in(hostfs_volume *vol, int parent_index)
{
    char (*current)[HOSTFS_BASENAME_MAX];
    char (*previous)[HOSTFS_BASENAME_MAX];
    int count;
    int prev_count;
    char path[HOSTFS_PATH_MAX];
    char parent_path[HOSTFS_PATH_MAX];
    FILE *fp;
    int i;

    if (vol == NULL) {
        return A2_ERR;
    }
    hostfs_parent_host_path(vol, parent_index, parent_path, sizeof(parent_path));
    current = NULL;
    previous = NULL;
    count = 0;
    prev_count = 0;
    if (hostfs_catalog_basenames_in(vol, parent_index, &current, &count) != A2_OK ||
        hostfs_load_order_file(parent_path, &previous, &prev_count) != A2_OK) {
        free(current);
        free(previous);
        return A2_ERR;
    }
    if (parent_index < 0) {
        if (hostfs_order_lists_equal(current, count, vol->order_basenames, vol->order_count)) {
            free(current);
            free(previous);
            return A2_OK;
        }
        /* The catalog is a truncated view of the host root, so rewriting the
           manifest from it would drop the user's ordering for everything past
           the cap - from their own file, permanently. Freeze instead: the
           equality check above ran first, so this only fires when a write really
           would have happened. Clears on the next mount, not live. */
        if (vol->root_truncated) {
            if (!vol->order_frozen_warned) {
                hostfs_warn(
                    vol, "%s: " HOSTFS_ORDER_FILENAME
                         " left unchanged while the root is over the limit",
                    vol->root_path);
                vol->order_frozen_warned = true;
            }
            free(current);
            free(previous);
            return A2_OK;
        }
    } else if (hostfs_order_lists_equal(current, count, previous, prev_count)) {
        free(current);
        free(previous);
        return A2_OK;
    }

    hostfs_path_join(path, sizeof(path), parent_path, HOSTFS_ORDER_FILENAME);
    fp = fopen(path, "w");
    if (fp == NULL) {
        free(current);
        free(previous);
        return A2_ERR;
    }
    fprintf(fp, "# a2m HostFS catalog order (NAPS files and directory basenames)\n");
    for (i = 0; i < count; ++i) {
        fprintf(fp, "%s\n", current[i]);
    }
    fclose(fp);
    vol->host_directory_changed = 1;

    if (parent_index < 0) {
        /* The volume directory cannot hold more than this, but the catalog walk
           is bounded by the scratch buffer, not by the root limit. */
        vol->order_count = count < HOSTFS_ROOT_MAX_ENTRIES ? count : HOSTFS_ROOT_MAX_ENTRIES;
        for (i = 0; i < vol->order_count; ++i) {
            snprintf(vol->order_basenames[i], HOSTFS_BASENAME_MAX, "%s", current[i]);
        }
    }
    free(current);
    free(previous);
    return A2_OK;
}

static int hostfs_persist_order_manifest(hostfs_volume *vol)
{
    return hostfs_persist_order_manifest_in(vol, -1);
}

static int hostfs_scan_dir_recursive(
    hostfs_volume *vol, const char *host_dir_path, int parent_index, int depth)
{
    hostfs_scan_ent *scans;
    char (*order_names)[HOSTFS_BASENAME_MAX];
    int order_count;
    int n;
    int i;
    int rc = A2_OK;
    int accepted = 0;
    int dropped = 0;
    int named = 0;
    char drop_list[160];

    if (depth > HOSTFS_MAX_DEPTH) {
        return A2_OK;
    }
    hostfs_watch_scanned_directory(vol, parent_index);
    drop_list[0] = '\0';

    scans = NULL;
    order_names = NULL;
    n = 0;
    order_count = 0;
    if (hostfs_collect_scans_in(host_dir_path, &scans, &n) != A2_OK ||
        hostfs_load_order_file(host_dir_path, &order_names, &order_count) != A2_OK) {
        rc = A2_ERR;
        goto done;
    }
    if (order_count > 0) {
        if (hostfs_apply_order_to_scans(scans, n, order_names, order_count) != A2_OK) {
            rc = A2_ERR;
            goto done;
        }
    } else if (n > 1) {
        qsort(scans, (size_t)n, sizeof(scans[0]), hostfs_scan_ent_basename_cmp);
    }

    for (i = 0; i < n; ++i) {
        int fi;
        if (hostfs_active_name_exists(vol, parent_index, scans[i].prodos, -1)) {
            continue;
        }
        /* The volume directory cannot grow past its four blocks. Name the host
           basenames, not the ProDOS names: a dropped entry never enters the
           catalog, so its ProDOS name exists nowhere the user can look. */
        if (parent_index < 0 && accepted >= HOSTFS_ROOT_MAX_ENTRIES) {
            dropped++;
            if (named < 5) {
                size_t used = strlen(drop_list);
                snprintf(
                    drop_list + used, sizeof(drop_list) - used, "%s%s",
                    named > 0 ? ", " : "", scans[i].basename);
                named++;
            }
            continue;
        }
        fi = hostfs_alloc_file_slot(vol);
        if (fi < 0) {
            break;
        }
        {
            /* Scoped so no pointer into the node table is live when the
               recursive descent below grows and reallocates it. */
            hostfs_node *file = &vol->nodes[fi];
            memset(file, 0, sizeof(*file));
            file->active = true;
            file->kind = (uint8_t)scans[i].kind;
            file->parent_index = parent_index;
            snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", scans[i].prodos);
            file->name_len = (uint8_t)strlen(file->prodos_name);
            file->file_type = scans[i].type;
            file->aux_type = scans[i].aux;
            file->host_mtime = (int64_t)scans[i].mtime;
            /* Grows vol->names, never vol->nodes, so `file` stays valid. */
            file->name_off = hostfs_names_add(vol, scans[i].basename);

            if (scans[i].kind == HOSTFS_KIND_DIR) {
                file->storage_type = HOSTFS_STOR_SUBDIR;
                file->file_type = HOSTFS_FILE_TYPE_DIR;
                file->eof = 0;
                file->host_size = 0;
            } else {
                file->eof =
                    scans[i].size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)scans[i].size;
                file->host_size =
                    scans[i].size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)scans[i].size;
            }
        }

        if (scans[i].kind == HOSTFS_KIND_DIR && depth < HOSTFS_MAX_DEPTH) {
            /* Derive into a local: the callee grows the node table, so the old
               file->host_path argument dangled for the whole subtree scan. */
            char child_path[HOSTFS_PATH_MAX];
            if (hostfs_node_host_path(vol, fi, child_path, sizeof(child_path)) != A2_OK ||
                hostfs_scan_dir_recursive(vol, child_path, fi, depth + 1) != A2_OK) {
                rc = A2_ERR;
                goto done;
            }
        }

        accepted++;
        if (parent_index < 0 && vol->order_count < HOSTFS_ROOT_MAX_ENTRIES) {
            snprintf(
                vol->order_basenames[vol->order_count], HOSTFS_BASENAME_MAX, "%s",
                scans[i].basename);
            vol->order_count++;
        }
    }

    if (dropped > 0) {
        char tail[32];
        tail[0] = '\0';
        if (dropped > named) {
            snprintf(tail, sizeof(tail), " (+%d more)", dropped - named);
        }
        vol->root_truncated = true;
        /* One accumulated line per scan, so a badly shaped folder cannot flood. */
        hostfs_warn(
            vol, "%s: volume directory holds %d entries (ProDOS limit); dropped %d: %s%s",
            vol->root_path, HOSTFS_ROOT_MAX_ENTRIES, dropped, drop_list, tail);
    }

done:
    free(scans);
    free(order_names);
    return rc;
}

static int hostfs_scan_into_files(hostfs_volume *vol)
{
    /* A full rescan rebuilds the arena from scratch, which is what keeps the
       append-on-rename waste bounded. */
    vol->node_count = 0;
    vol->order_count = 0;
    vol->names_len = 0;
    return hostfs_scan_dir_recursive(vol, vol->root_path, -1, 0);
}

void hostfs_eject(hostfs_volume *vol)
{
    int i;
    if (vol == NULL) {
        return;
    }
    fs_watch_destroy(vol->watch);
    vol->watch = NULL;
    (void)hostfs_flush(vol);
    for (i = 0; i < vol->map_count; ++i) {
        if (vol->map[i].kind == HOSTFS_MAP_RAM) {
            free(vol->map[i].u.ram);
        }
    }
    free(vol->map);
    free(vol->block_to_map);
    free(vol->bitmap);
    free(vol->nodes);
    free(vol->names);
    free(vol->synthetic_events);
    free(vol->dirty_parents);
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
    vol->block_to_map = (int32_t *)malloc(
        ((size_t)HOSTFS_TOTAL_BLOCKS + 1u) * sizeof(*vol->block_to_map));
    if (vol->block_to_map == NULL) {
        free(vol);
        return NULL;
    }
    for (i = 0; i <= HOSTFS_TOTAL_BLOCKS; ++i) {
        vol->block_to_map[i] = -1;
    }
    snprintf(vol->root_path, sizeof(vol->root_path), "%s", root_path);
    if (!hostfs_mangle_prodos_name(volume_name, vol->volume_name, sizeof(vol->volume_name))) {
        snprintf(vol->volume_name, sizeof(vol->volume_name), "HOSTFS");
    }
    vol->volume_name_len = (uint8_t)strlen(vol->volume_name);
    vol->total_blocks = (uint16_t)HOSTFS_TOTAL_BLOCKS;
    /* Arm notifications before the initial scan. Events queued while scanning
       are deliberately retained and replayed on the first SmartPort touch. */
    vol->watch = fs_watch_create(vol->root_path);
    if (vol->watch == NULL) {
        hostfs_enter_periodic_refresh(vol);
    }

    if (hostfs_scan_into_files(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }
    if (vol->watch_coverage_failed) {
        hostfs_enter_periodic_refresh(vol);
    }

    /* The ProDOS volume directory is always blocks 2-5 and cannot be extended, so
       the root never consults hostfs_dir_blocks_for_files. That puts the volume
       bitmap at a constant block 6, as on a real volume. */
    vol->dir_block_count = (uint16_t)HOSTFS_ROOT_DIR_BLOCKS;
    for (i = 0; i < vol->node_count; ++i) {
        int children;
        uint16_t blocks;
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_DIR) {
            continue;
        }
        children = hostfs_child_count(vol, i);
        blocks = hostfs_dir_blocks_for_files(children);
        if (blocks < 2u && children > 0) {
            blocks = 2u;
        }
        if (blocks < 1u) {
            blocks = 1u;
        }
        vol->nodes[i].dir_block_count = blocks;
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

    /* Bitmap buffer before file/dir alloc so marks work. */
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

    /* Directories first so their key blocks exist before file storage. */
    for (i = 0; i < vol->node_count; ++i) {
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_DIR) {
            continue;
        }
        if (hostfs_alloc_dir_storage(vol, i) != A2_OK) {
            hostfs_eject(vol);
            return NULL;
        }
    }
    for (i = 0; i < vol->node_count; ++i) {
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_FILE) {
            continue;
        }
        if (hostfs_alloc_file_storage(vol, i) != A2_OK) {
            hostfs_eject(vol);
            return NULL;
        }
    }

    if (hostfs_build_all_directories(vol) != A2_OK || hostfs_sync_bitmap_to_map(vol) != A2_OK) {
        hostfs_eject(vol);
        return NULL;
    }
    hostfs_map_assert_consistent(vol);
    /* Mount scan counts as a successful refresh; first touch within delta is free. */
    vol->last_refresh_ms = hostfs_now_ms();
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
        if (fi < 0 || fi >= vol->node_count || !vol->nodes[fi].active) {
            return A2_ERR;
        }
        char path[HOSTFS_PATH_MAX];
        if (hostfs_node_host_path(vol, fi, path, sizeof(path)) != A2_OK) {
            return A2_ERR;
        }
        fp = fopen(path, "rb");
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
    hostfs_node *file = &vol->nodes[file_index];
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

/* ---- directory reconcile (Phase 3 + 5b) ---- */

static int hostfs_count_dir_chain(hostfs_volume *vol, uint16_t key_block)
{
    uint16_t block = key_block;
    uint16_t guard = 0;
    int count = 0;
    while (block != 0u && guard++ < 1024u) {
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        count++;
        if (dir == NULL) {
            break;
        }
        block = hostfs_read_u16(dir + 2);
    }
    return count;
}

static int hostfs_adopt_dir_storage(hostfs_volume *vol, int dir_index)
{
    hostfs_node *dir_node;
    uint16_t block;
    uint16_t guard = 0;
    uint16_t count = 0;

    if (dir_index < 0 || dir_index >= vol->node_count) {
        return A2_ERR;
    }
    dir_node = &vol->nodes[dir_index];
    block = dir_node->key_block;
    if (block == 0u) {
        return A2_ERR;
    }
    while (block != 0u && guard++ < 1024u) {
        if (hostfs_map_find(vol, block) < 0) {
            if (hostfs_map_add_ram(vol, block, NULL) != A2_OK) {
                return A2_ERR;
            }
        }
        hostfs_bitmap_mark_used(vol->bitmap, block);
        count++;
        {
            uint8_t *dir = hostfs_map_ram_ptr(vol, block);
            if (dir == NULL) {
                break;
            }
            block = hostfs_read_u16(dir + 2);
        }
    }
    if (count == 0u) {
        count = 1u;
    }
    dir_node->dir_block_count = count;
    dir_node->blocks_used = count;
    dir_node->eof = (uint32_t)count * HOSTFS_BLOCK_SIZE;
    dir_node->storage_type = HOSTFS_STOR_SUBDIR;
    dir_node->file_type = HOSTFS_FILE_TYPE_DIR;
    return A2_OK;
}

static void hostfs_ensure_subdir_header(hostfs_volume *vol, int dir_index)
{
    hostfs_node *dir_node;
    uint8_t *dir;
    uint8_t st;
    if (dir_index < 0 || dir_index >= vol->node_count) {
        return;
    }
    dir_node = &vol->nodes[dir_index];
    dir = hostfs_map_ram_ptr(vol, dir_node->key_block);
    if (dir == NULL) {
        if (hostfs_map_add_ram(vol, dir_node->key_block, NULL) != A2_OK) {
            return;
        }
        dir = hostfs_map_ram_ptr(vol, dir_node->key_block);
        if (dir == NULL) {
            return;
        }
    }
    st = (uint8_t)(dir[4] >> 4);
    if (st != HOSTFS_STOR_SUBDIR_HDR) {
        memset(dir, 0, HOSTFS_BLOCK_SIZE);
        hostfs_write_u16(dir + 0, 0);
        hostfs_write_u16(dir + 2, 0);
        hostfs_fill_subdir_header(
            dir + 4, dir_node, dir_node->parent_entry_block, dir_node->parent_entry_number,
            (uint16_t)hostfs_child_count(vol, dir_index));
    } else {
        hostfs_fill_subdir_header(
            dir + 4, dir_node, dir_node->parent_entry_block, dir_node->parent_entry_number,
            (uint16_t)hostfs_child_count(vol, dir_index));
    }
}

static void hostfs_destroy_reconciled_node(hostfs_volume *vol, int index)
{
    char path[HOSTFS_PATH_MAX];
    int i;
    if (index < 0 || index >= vol->node_count || !vol->nodes[index].active) {
        return;
    }
    if (hostfs_node_host_path(vol, index, path, sizeof(path)) != A2_OK) {
        vol->nodes[index].active = false;
        return;
    }
    if (vol->nodes[index].kind == HOSTFS_KIND_DIR) {
        char order_path[HOSTFS_PATH_MAX];
        for (i = 0; i < vol->node_count; ++i) {
            if (vol->nodes[i].active && vol->nodes[i].parent_index == index) {
                hostfs_destroy_reconciled_node(vol, i);
            }
        }
        /* Order manifest is HostFS metadata, not a ProDOS file - drop it so rmdir can succeed. */
        hostfs_path_join(order_path, sizeof(order_path), path, HOSTFS_ORDER_FILENAME);
        (void)remove(order_path);
        if (hostfs_rmdir(path) != 0) {
            hostfs_warn(vol, "rmdir failed: %s", path);
        }
        hostfs_map_remove_dir_blocks(vol, index);
    } else {
        (void)remove(path);
        hostfs_map_remove_file(vol, index);
    }
    vol->nodes[index].active = false;
}

static int hostfs_reconcile_directory_at(hostfs_volume *vol, int parent_index)
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
        uint16_t entry_block;
        uint8_t entry_number;
    } dent;

    dent *ents = NULL;
    size_t ent_cap;
    int ent_count = 0;
    uint16_t d;
    uint16_t blocks;
    int i;
    hostfs_marks seen;
    char parent_path[HOSTFS_PATH_MAX];

    hostfs_marks_init(&seen);

    /* Refresh dir_block_count for subdirs in case ProDOS grew the chain. */
    if (parent_index >= 0 && parent_index < vol->node_count &&
        vol->nodes[parent_index].active && vol->nodes[parent_index].kind == HOSTFS_KIND_DIR) {
        int chain = hostfs_count_dir_chain(vol, vol->nodes[parent_index].key_block);
        if (chain > 0) {
            vol->nodes[parent_index].dir_block_count = (uint16_t)chain;
            vol->nodes[parent_index].blocks_used = (uint16_t)chain;
            vol->nodes[parent_index].eof = (uint32_t)chain * HOSTFS_BLOCK_SIZE;
        }
    }

    blocks = hostfs_dir_block_count_of(vol, parent_index);
    ent_cap = (size_t)blocks * HOSTFS_ENTRIES_PER_BLOCK;
    if (ent_cap > 0u) {
        ents = (dent *)calloc(ent_cap, sizeof(*ents));
        if (ents == NULL) {
            hostfs_marks_free(&seen);
            return A2_ERR;
        }
    }
    hostfs_parent_host_path(vol, parent_index, parent_path, sizeof(parent_path));

    for (d = 0; d < blocks; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = (d == 0u) ? 1 : 0;
        if (dir == NULL || block == 0u) {
            continue;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            uint8_t st;
            dent *de;
            if (e[0] == 0) {
                continue;
            }
            st = (uint8_t)(e[0] >> 4);
            if (st == HOSTFS_STOR_VOL_HDR || st == HOSTFS_STOR_SUBDIR_HDR) {
                continue;
            }
            if (!((st >= HOSTFS_STOR_SEEDLING && st <= HOSTFS_STOR_TREE) ||
                  st == HOSTFS_STOR_SUBDIR)) {
                continue;
            }
            assert((size_t)ent_count < ent_cap);
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
            de->entry_block = block;
            de->entry_number = (uint8_t)(slot + 1);
        }
    }

    for (i = 0; i < ent_count; ++i) {
        dent *de = &ents[i];
        int fi = hostfs_find_by_key(vol, de->key_block);
        if (fi < 0) {
            fi = hostfs_find_by_name(vol, parent_index, de->name);
        }
        if (fi < 0) {
            hostfs_node *file;
            char made[HOSTFS_PATH_MAX];
            fi = hostfs_alloc_file_slot(vol);
            if (fi < 0) {
                continue;
            }
            file = &vol->nodes[fi];
            memset(file, 0, sizeof(*file));
            file->active = true;
            file->parent_index = parent_index;
            snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", de->name);
            file->name_len = de->name_len;
            file->file_type = de->file_type;
            file->aux_type = de->aux_type;
            file->eof = de->eof;
            file->storage_type = de->storage_type;
            file->key_block = de->key_block;
            file->blocks_used = de->blocks_used;
            file->parent_entry_block = de->entry_block;
            file->parent_entry_number = de->entry_number;

            if (de->storage_type == HOSTFS_STOR_SUBDIR) {
                file->kind = HOSTFS_KIND_DIR;
                file->file_type = HOSTFS_FILE_TYPE_DIR;
                if (hostfs_create_or_reuse_host_dir(
                        vol, parent_index, de->name, made, sizeof(made)) != A2_OK) {
                    file->active = false;
                    continue;
                }
                hostfs_node_set_basename(vol, fi, hostfs_path_basename(made));
                if (hostfs_adopt_dir_storage(vol, fi) != A2_OK) {
                    file->active = false;
                    continue;
                }
                hostfs_ensure_subdir_header(vol, fi);
            } else {
                file->kind = HOSTFS_KIND_FILE;
                if (hostfs_create_or_reuse_host_file(
                        vol, parent_index, de->name, de->file_type, de->aux_type, made,
                        sizeof(made)) != A2_OK) {
                    file->active = false;
                    continue;
                }
                hostfs_node_set_basename(vol, fi, hostfs_path_basename(made));
                if (hostfs_bind_storage_to_host(vol, fi) != A2_OK) {
                    vol->internal_full_rescan_required = true;
                }
                if (de->eof > 0u &&
                    hostfs_host_truncate(vol, fi, de->eof) != A2_OK) {
                    vol->internal_full_rescan_required = true;
                }
            }
            hostfs_marks_set(&seen, fi);
        } else {
            hostfs_node *file = &vol->nodes[fi];
            hostfs_marks_set(&seen, fi);
            file->parent_entry_block = de->entry_block;
            file->parent_entry_number = de->entry_number;

            if (file->parent_index != parent_index) {
                /* Cross-directory move: relocate the host file under the new
                   parent. Descendants of a moved directory need no fixup - they
                   derive their paths through this node. */
                char dest[HOSTFS_PATH_MAX];
                char old_path[HOSTFS_PATH_MAX];
                if (hostfs_node_host_path(vol, fi, old_path, sizeof(old_path)) != A2_OK) {
                    continue;
                }
                if (file->kind == HOSTFS_KIND_DIR) {
                    hostfs_path_join(dest, sizeof(dest), parent_path, de->name);
                } else {
                    char naps[HOSTFS_PATH_MAX];
                    if (!hostfs_compose_naps_filename(
                            de->name, de->file_type, de->aux_type, naps, sizeof(naps))) {
                        continue;
                    }
                    hostfs_path_join(dest, sizeof(dest), parent_path, naps);
                }
                if (strcmp(old_path, dest) == 0 || rename(old_path, dest) == 0) {
                    hostfs_node_set_basename(vol, fi, hostfs_path_basename(dest));
                } else {
                    hostfs_warn(
                        vol, "move failed: %s -> %s (catalog moved, host file did not)",
                        old_path, dest);
                }
                /* The parent moves whether or not the host rename worked. If it
                   did not, the node points at a path that does not exist and the
                   next rescan cleans it up - but leaving parent_index behind
                   would make the old parent's next reconcile see an entry that
                   vanished from its catalog and delete the real host file. */
                file->parent_index = parent_index;
            }

            if (file->kind == HOSTFS_KIND_DIR || de->storage_type == HOSTFS_STOR_SUBDIR) {
                if (strcmp(file->prodos_name, de->name) != 0) {
                    char dest[HOSTFS_PATH_MAX];
                    char old_path[HOSTFS_PATH_MAX];
                    if (hostfs_node_host_path(vol, fi, old_path, sizeof(old_path)) == A2_OK) {
                        hostfs_path_join(dest, sizeof(dest), parent_path, de->name);
                        if (strcmp(old_path, dest) == 0) {
                            /* Already there. */
                        } else if (rename(old_path, dest) == 0) {
                            hostfs_node_set_basename(vol, fi, hostfs_path_basename(dest));
                        } else {
                            hostfs_warn(
                                vol, "rename failed: %s -> %s (catalog renamed, host "
                                     "directory did not)",
                                old_path, dest);
                        }
                    }
                    snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", de->name);
                    file->name_len = de->name_len;
                }
                file->kind = HOSTFS_KIND_DIR;
                file->storage_type = HOSTFS_STOR_SUBDIR;
                file->file_type = HOSTFS_FILE_TYPE_DIR;
                file->key_block = de->key_block;
                file->blocks_used = de->blocks_used;
                file->eof = de->eof;
                (void)hostfs_adopt_dir_storage(vol, fi);
                hostfs_ensure_subdir_header(vol, fi);
            } else {
                if (strcmp(file->prodos_name, de->name) != 0 ||
                    file->file_type != de->file_type || file->aux_type != de->aux_type) {
                    char naps[HOSTFS_PATH_MAX];
                    char dest[HOSTFS_PATH_MAX];
                    char old_path[HOSTFS_PATH_MAX];
                    if (hostfs_compose_naps_filename(
                            de->name, de->file_type, de->aux_type, naps, sizeof(naps)) &&
                        hostfs_node_host_path(vol, fi, old_path, sizeof(old_path)) == A2_OK) {
                        hostfs_path_join(dest, sizeof(dest), parent_path, naps);
                        if (strcmp(old_path, dest) != 0) {
                            if (rename(old_path, dest) == 0) {
                                hostfs_node_set_basename(
                                    vol, fi, hostfs_path_basename(dest));
                            } else {
                                hostfs_warn(
                                    vol, "rename failed: %s -> %s (catalog renamed, host "
                                         "file did not)",
                                    old_path, dest);
                            }
                        }
                    }
                    snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", de->name);
                    file->name_len = de->name_len;
                    file->file_type = de->file_type;
                    file->aux_type = de->aux_type;
                }
                if (de->eof < file->eof) {
                    if (hostfs_host_truncate(vol, fi, de->eof) != A2_OK) {
                        vol->internal_full_rescan_required = true;
                    }
                }
                file->eof = de->eof;
                file->storage_type = de->storage_type;
                file->key_block = de->key_block;
                file->blocks_used = de->blocks_used;
                if (hostfs_bind_storage_to_host(vol, fi) != A2_OK) {
                    vol->internal_full_rescan_required = true;
                }
            }
        }
    }

    for (i = 0; i < vol->node_count; ++i) {
        if (!vol->nodes[i].active || hostfs_marks_get(&seen, i)) {
            continue;
        }
        if (vol->nodes[i].parent_index != parent_index) {
            continue;
        }
        hostfs_destroy_reconciled_node(vol, i);
    }
    hostfs_marks_free(&seen);
    free(ents);

    (void)hostfs_patch_dir_file_count(vol, parent_index);
    (void)hostfs_sync_bitmap_to_map(vol);
    (void)hostfs_persist_order_manifest_in(vol, parent_index);
    hostfs_map_assert_consistent(vol);
    return A2_OK;
}

static bool hostfs_find_dir_owner(hostfs_volume *vol, uint16_t block, int *out_parent)
{
    int i;
    uint8_t *blk;
    uint16_t prev;

    if (block >= 2u && block < (uint16_t)(2u + vol->dir_block_count)) {
        *out_parent = -1;
        return true;
    }

    for (i = 0; i < vol->node_count; ++i) {
        uint16_t b;
        uint16_t guard = 0;
        uint16_t count = 0;
        if (!vol->nodes[i].active || vol->nodes[i].kind != HOSTFS_KIND_DIR) {
            continue;
        }
        b = vol->nodes[i].key_block;
        while (b != 0u && guard++ < 1024u) {
            count++;
            if (b == block) {
                if (count > vol->nodes[i].dir_block_count) {
                    vol->nodes[i].dir_block_count = count;
                    vol->nodes[i].blocks_used = count;
                    vol->nodes[i].eof = (uint32_t)count * HOSTFS_BLOCK_SIZE;
                }
                *out_parent = i;
                return true;
            }
            {
                uint8_t *dir = hostfs_map_ram_ptr(vol, b);
                if (dir == NULL) {
                    break;
                }
                b = hostfs_read_u16(dir + 2);
            }
        }
    }

    blk = hostfs_map_ram_ptr(vol, block);
    if (blk != NULL) {
        prev = hostfs_read_u16(blk + 0);
        if (prev != 0u && hostfs_find_dir_owner(vol, prev, out_parent)) {
            if (*out_parent >= 0 && *out_parent < vol->node_count) {
                uint16_t n = vol->nodes[*out_parent].dir_block_count;
                vol->nodes[*out_parent].dir_block_count = (uint16_t)(n + 1u);
                vol->nodes[*out_parent].blocks_used = vol->nodes[*out_parent].dir_block_count;
                vol->nodes[*out_parent].eof =
                    (uint32_t)vol->nodes[*out_parent].dir_block_count * HOSTFS_BLOCK_SIZE;
            }
            return true;
        }
    }
    return false;
}

int hostfs_write_block(hostfs_volume *vol, uint32_t block, const uint8_t *data)
{
    int idx;
    int rc = A2_OK;
    int owner;

    if (vol == NULL || data == NULL || block >= vol->total_blocks) {
        return A2_ERR;
    }

    if (vol->apple != NULL && vol->apple->replay_sealed) {
        return A2_OK;
    }

    vol->guest_write_depth++;

    idx = hostfs_map_find(vol, (uint16_t)block);
    if (idx >= 0 && vol->map[idx].kind == HOSTFS_MAP_HOST) {
        int file_index = vol->map[idx].u.host.file_index;
        rc = hostfs_host_write(
            vol, file_index, vol->map[idx].u.host.offset, data,
            HOSTFS_BLOCK_SIZE);
        if (rc != A2_OK && file_index >= 0 && file_index < vol->node_count) {
            hostfs_schedule_directory_refresh(vol, vol->nodes[file_index].parent_index);
        }
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

    if (rc == A2_OK && hostfs_find_dir_owner(vol, (uint16_t)block, &owner)) {
        rc = hostfs_reconcile_directory_at(vol, owner);
        /* Successful guest operations will usually produce watcher echoes;
           failed host operations may not. Scheduling the authoritative parent
           makes both cases converge through the same idempotent owner-thread
           reconciliation path after guest_write_depth unwinds. */
        hostfs_schedule_directory_refresh(vol, owner);
    }

    vol->guest_write_depth--;
    if (rc == A2_OK) {
        apple2_note_media_event(
            vol->apple,
            vol->slot,
            vol->device,
            APPLE2_MEDIA_EVENT_GUEST_WRITE);
    }
    return rc;
}

/* ---- Phase 2 host → volume ---- */

static int hostfs_grow_file_storage(hostfs_volume *vol, int file_index, uint32_t new_eof)
{
    hostfs_node *file = &vol->nodes[file_index];
    uint32_t old_blocks =
        (file->eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    uint32_t new_blocks = (new_eof + HOSTFS_BLOCK_SIZE - 1u) / HOSTFS_BLOCK_SIZE;
    uint8_t need_st;
    uint16_t parent_key;

    if (file->kind != HOSTFS_KIND_FILE) {
        return A2_ERR;
    }
    if (new_eof == 0u) {
        new_blocks = 1u;
    }
    if (old_blocks == 0u) {
        old_blocks = 1u;
    }
    if (new_blocks <= 1u) {
        need_st = HOSTFS_STOR_SEEDLING;
    } else if (new_blocks <= 256u) {
        need_st = HOSTFS_STOR_SAPLING;
    } else {
        need_st = HOSTFS_STOR_TREE;
    }

    if (need_st != file->storage_type || new_blocks < old_blocks) {
        hostfs_map_remove_file(vol, file_index);
        file->eof = new_eof;
        if (hostfs_alloc_file_storage(vol, file_index) != A2_OK) {
            return A2_ERR;
        }
    } else if (new_blocks > old_blocks && file->storage_type == HOSTFS_STOR_SAPLING) {
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
    } else if (new_blocks > old_blocks && file->storage_type == HOSTFS_STOR_SEEDLING &&
               need_st == HOSTFS_STOR_SEEDLING) {
        file->eof = new_eof;
    } else {
        file->eof = new_eof;
    }

    parent_key = hostfs_parent_key(vol, file->parent_index);
    {
        uint8_t *entry =
            hostfs_find_dir_entry_slot(vol, file->parent_index, file->prodos_name, NULL);
        if (entry != NULL) {
            hostfs_fill_file_entry(entry, file, parent_key);
        }
    }
    (void)hostfs_sync_bitmap_to_map(vol);
    return A2_OK;
}

static int hostfs_grow_subdir_capacity(hostfs_volume *vol, int dir_index)
{
    hostfs_node *dir_node;
    uint16_t new_block;
    uint16_t last_block;
    uint8_t *prev_dir;
    uint8_t *dir;
    uint16_t old_n;

    if (dir_index < 0) {
        return A2_ERR; /* volume directory is fixed size */
    }
    dir_node = &vol->nodes[dir_index];
    old_n = dir_node->dir_block_count;
    if (old_n == 0u) {
        return A2_ERR;
    }
    last_block = hostfs_dir_block_at(vol, dir_index, (uint16_t)(old_n - 1u));
    if (last_block == 0u) {
        return A2_ERR;
    }
    new_block = hostfs_alloc_block(vol);
    if (new_block == 0u) {
        return A2_ERR;
    }
    if (hostfs_map_add_ram(vol, new_block, NULL) != A2_OK) {
        return A2_ERR;
    }
    hostfs_bitmap_mark_used(vol->bitmap, new_block);
    dir = hostfs_map_ram_ptr(vol, new_block);
    memset(dir, 0, HOSTFS_BLOCK_SIZE);
    hostfs_write_u16(dir + 0, last_block);
    hostfs_write_u16(dir + 2, 0);
    prev_dir = hostfs_map_ram_ptr(vol, last_block);
    if (prev_dir != NULL) {
        hostfs_write_u16(prev_dir + 2, new_block);
    }
    dir_node->dir_block_count = (uint16_t)(old_n + 1u);
    dir_node->blocks_used = dir_node->dir_block_count;
    dir_node->eof = (uint32_t)dir_node->dir_block_count * HOSTFS_BLOCK_SIZE;
    {
        uint8_t *entry = hostfs_find_dir_entry_slot(
            vol, dir_node->parent_index, dir_node->prodos_name, NULL);
        if (entry != NULL) {
            hostfs_fill_file_entry(
                entry, dir_node, hostfs_parent_key(vol, dir_node->parent_index));
        }
    }
    return A2_OK;
}

static void hostfs_deactivate_node(hostfs_volume *vol, int index)
{
    int i;
    if (index < 0 || index >= vol->node_count || !vol->nodes[index].active) {
        return;
    }
    if (vol->nodes[index].kind == HOSTFS_KIND_DIR) {
        for (i = 0; i < vol->node_count; ++i) {
            if (vol->nodes[i].active && vol->nodes[i].parent_index == index) {
                hostfs_deactivate_node(vol, i);
            }
        }
        hostfs_map_remove_dir_blocks(vol, index);
    } else {
        hostfs_map_remove_file(vol, index);
    }
    {
        uint8_t *entry = hostfs_find_dir_entry_slot(
            vol, vol->nodes[index].parent_index, vol->nodes[index].prodos_name, NULL);
        if (entry != NULL) {
            memset(entry, 0, HOSTFS_ENTRY_LENGTH);
        }
    }
    (void)hostfs_patch_dir_file_count(vol, vol->nodes[index].parent_index);
    vol->nodes[index].active = false;
}

static int hostfs_add_node_from_scan(
    hostfs_volume *vol, int parent_index, const hostfs_scan_ent *sc)
{
    int fi;
    hostfs_node *file;
    uint8_t *entry;
    uint16_t parent_key = hostfs_parent_key(vol, parent_index);

    if (hostfs_active_name_exists(vol, parent_index, sc->prodos, -1)) {
        return A2_OK;
    }
    entry = hostfs_first_free_dir_entry(vol, parent_index);
    if (entry == NULL && parent_index >= 0) {
        if (hostfs_grow_subdir_capacity(vol, parent_index) == A2_OK) {
            entry = hostfs_first_free_dir_entry(vol, parent_index);
        }
    }
    if (entry == NULL) {
        if (parent_index < 0) {
            /* The root is out of entries and cannot grow, so a remount will not
               help - the folder itself has to change. Mark the catalog truncated
               so the order manifest is not rewritten short: hostfs_rescan never
               re-runs the mount scan, so this is the only place a root that fills
               up after mount can be caught. */
            vol->root_truncated = true;
            if (!vol->root_full_warned) {
                hostfs_warn(
                    vol,
                    "%s: volume directory is full (%d entries, ProDOS limit); "
                    "'%s' not added - move it into a folder",
                    vol->root_path, HOSTFS_ROOT_MAX_ENTRIES, sc->basename);
                vol->root_full_warned = true;
            }
        } else if (!vol->subdir_full_warned) {
            char dir_path[HOSTFS_PATH_MAX];
            hostfs_parent_host_path(vol, parent_index, dir_path, sizeof(dir_path));
            hostfs_warn(vol, "%s: directory full; remount to pick up new files", dir_path);
            vol->subdir_full_warned = true;
        }
        return A2_ERR;
    }
    fi = hostfs_alloc_file_slot(vol);
    if (fi < 0) {
        return A2_ERR;
    }
    file = &vol->nodes[fi];
    memset(file, 0, sizeof(*file));
    file->active = true;
    file->kind = sc->kind;
    file->parent_index = parent_index;
    snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", sc->prodos);
    file->name_len = (uint8_t)strlen(file->prodos_name);
    file->file_type = sc->type;
    file->aux_type = sc->aux;
    file->host_mtime = (int64_t)sc->mtime;
    hostfs_node_set_basename(vol, fi, sc->basename);

    if (sc->kind == HOSTFS_KIND_DIR) {
        file->storage_type = HOSTFS_STOR_SUBDIR;
        file->file_type = HOSTFS_FILE_TYPE_DIR;
        file->dir_block_count = 1u;
        if (hostfs_alloc_dir_storage(vol, fi) != A2_OK) {
            file->active = false;
            return A2_ERR;
        }
        if (hostfs_build_subdirectory(vol, fi) != A2_OK) {
            hostfs_map_remove_dir_blocks(vol, fi);
            file->active = false;
            return A2_ERR;
        }
    } else {
        file->eof = sc->size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)sc->size;
        file->host_size = sc->size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)sc->size;
        if (hostfs_alloc_file_storage(vol, fi) != A2_OK) {
            file->active = false;
            return A2_ERR;
        }
    }

    hostfs_fill_file_entry(entry, file, parent_key);
    /* parent_entry for new dirs: locate slot we just wrote */
    {
        uint16_t d;
        uint16_t count = hostfs_dir_block_count_of(vol, parent_index);
        for (d = 0; d < count; ++d) {
            uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
            uint8_t *dir = hostfs_map_ram_ptr(vol, block);
            int slot;
            int start = (d == 0u) ? 1 : 0;
            if (dir == NULL) {
                continue;
            }
            for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
                uint8_t *e = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
                if (e == entry) {
                    hostfs_note_parent_entry(vol, fi, block, slot);
                    if (file->kind == HOSTFS_KIND_DIR) {
                        uint8_t *sdir = hostfs_map_ram_ptr(vol, file->key_block);
                        if (sdir != NULL) {
                            hostfs_fill_subdir_header(
                                sdir + 4, file, block, (uint8_t)(slot + 1), 0);
                        }
                    }
                    break;
                }
            }
        }
    }
    (void)hostfs_patch_dir_file_count(vol, parent_index);
    (void)hostfs_sync_bitmap_to_map(vol);
    return A2_OK;
}

static int hostfs_rebuild_directory_order(
    hostfs_volume *vol,
    int parent_index,
    const hostfs_scan_ent *scans,
    int scan_count,
    bool snapshot_root_order)
{
    hostfs_marks written;
    int *order = NULL;
    int order_count = 0;
    int child_count = hostfs_child_count(vol, parent_index);
    uint16_t blocks = hostfs_dir_block_count_of(vol, parent_index);
    uint16_t d;
    int i;

    hostfs_marks_init(&written);
    if (child_count > 0) {
        order = (int *)malloc((size_t)child_count * sizeof(*order));
        if (order == NULL) {
            return A2_ERR;
        }
    }
    for (i = 0; i < scan_count; ++i) {
        int fi = hostfs_find_by_basename(vol, parent_index, scans[i].basename);
        if (fi >= 0 && vol->nodes[fi].active && !hostfs_marks_get(&written, fi)) {
            order[order_count++] = fi;
            hostfs_marks_set(&written, fi);
        }
    }
    /* Append any active child not already selected by basename. */
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].parent_index == parent_index &&
            !hostfs_marks_get(&written, i)) {
            order[order_count++] = i;
            hostfs_marks_set(&written, i);
        }
    }

    for (d = 0; d < blocks; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = d == 0u ? 1 : 0;
        if (dir == NULL) {
            hostfs_marks_free(&written);
            free(order);
            return A2_ERR;
        }
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK; ++slot) {
            memset(dir + 4 + slot * HOSTFS_ENTRY_LENGTH, 0, HOSTFS_ENTRY_LENGTH);
        }
    }

    i = 0;
    for (d = 0; d < blocks && i < order_count; ++d) {
        uint16_t block = hostfs_dir_block_at(vol, parent_index, d);
        uint8_t *dir = hostfs_map_ram_ptr(vol, block);
        int slot;
        int start = d == 0u ? 1 : 0;
        for (slot = start; slot < HOSTFS_ENTRIES_PER_BLOCK && i < order_count; ++slot) {
            int fi = order[i++];
            uint8_t *entry = dir + 4 + slot * HOSTFS_ENTRY_LENGTH;
            hostfs_fill_file_entry(entry, &vol->nodes[fi], hostfs_parent_key(vol, parent_index));
            hostfs_note_parent_entry(vol, fi, block, slot);
            if (vol->nodes[fi].kind == HOSTFS_KIND_DIR) {
                uint8_t *subdir = hostfs_map_ram_ptr(vol, vol->nodes[fi].key_block);
                if (subdir != NULL) {
                    hostfs_fill_subdir_header(
                        subdir + 4, &vol->nodes[fi], block, (uint8_t)(slot + 1),
                        (uint16_t)hostfs_child_count(vol, fi));
                }
            }
        }
    }
    if (i != order_count) {
        hostfs_marks_free(&written);
        free(order);
        return A2_ERR;
    }
    (void)hostfs_patch_dir_file_count(vol, parent_index);

    if (parent_index < 0 && snapshot_root_order && !vol->root_truncated) {
        vol->order_count = order_count < HOSTFS_ROOT_MAX_ENTRIES
                               ? order_count
                               : HOSTFS_ROOT_MAX_ENTRIES;
        for (i = 0; i < vol->order_count; ++i) {
            snprintf(
                vol->order_basenames[i], HOSTFS_BASENAME_MAX, "%s",
                hostfs_node_basename(vol, order[i]));
        }
    }
    hostfs_marks_free(&written);
    free(order);
    return A2_OK;
}

static int hostfs_update_file_from_scan(
    hostfs_volume *vol,
    int file_index,
    const hostfs_scan_ent *scan,
    bool preserve_guest_eof)
{
    hostfs_node *file = &vol->nodes[file_index];
    uint32_t new_eof =
        scan->size > 0x00FFFFFFu ? 0x00FFFFFFu : (uint32_t)scan->size;
    uint32_t new_host_size =
        scan->size > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)scan->size;

    snprintf(file->prodos_name, sizeof(file->prodos_name), "%s", scan->prodos);
    file->name_len = (uint8_t)strlen(file->prodos_name);
    file->file_type = scan->type;
    file->aux_type = scan->aux;
    file->host_mtime = (int64_t)scan->mtime;
    /* A guest block write can extend the physical host file past the ProDOS
       EOF while the directory remains authoritative. host_size records that
       expected write-through size, so its watcher echo must not grow the
       catalog. An external resize differs from host_size and is authoritative. */
    if (!(preserve_guest_eof && new_host_size == file->host_size) &&
        new_eof != file->eof &&
        hostfs_grow_file_storage(vol, file_index, new_eof) != A2_OK) {
        return A2_ERR;
    }
    file = &vol->nodes[file_index];
    file->host_size = new_host_size;
    return A2_OK;
}

/* Reconcile one authoritative host directory. Existing child directories are
   descended into only for a public/full scan; a newly discovered directory is
   always watched first and then materialized recursively. */
static int hostfs_rescan_directory_at(
    hostfs_volume *vol,
    int parent_index,
    int depth,
    bool recursive,
    bool targeted,
    bool preserve_guest_eof)
{
    hostfs_scan_ent *scans = NULL;
    char (*order_names)[HOSTFS_BASENAME_MAX] = NULL;
    char host_dir_path[HOSTFS_PATH_MAX];
    hostfs_marks matched;
    int order_count = 0;
    int n = 0;
    int i;
    int rc = A2_OK;

    if (depth > HOSTFS_MAX_DEPTH) {
        return A2_OK;
    }
    if (parent_index >= 0 &&
        (parent_index >= vol->node_count || !vol->nodes[parent_index].active ||
         vol->nodes[parent_index].kind != HOSTFS_KIND_DIR)) {
        return A2_ERR;
    }
    hostfs_parent_host_path(vol, parent_index, host_dir_path, sizeof(host_dir_path));
    hostfs_watch_scanned_directory(vol, parent_index);
    if (targeted) {
        vol->targeted_directory_scans++;
    }
    hostfs_marks_init(&matched);
    if (hostfs_collect_scans_in(host_dir_path, &scans, &n) != A2_OK ||
        hostfs_load_order_file(host_dir_path, &order_names, &order_count) != A2_OK) {
        rc = A2_ERR;
        goto done;
    }
    if (order_count > 0) {
        if (hostfs_apply_order_to_scans(scans, n, order_names, order_count) != A2_OK) {
            rc = A2_ERR;
            goto done;
        }
    } else if (n > 1) {
        qsort(scans, (size_t)n, sizeof(scans[0]), hostfs_scan_ent_basename_cmp);
    }

    for (i = 0; i < n; ++i) {
        char child_path[HOSTFS_PATH_MAX];
        int fi = hostfs_find_by_basename(vol, parent_index, scans[i].basename);
        bool added = false;

        if (fi < 0) {
            fi = hostfs_find_by_name(vol, parent_index, scans[i].prodos);
        }
        if (fi >= 0 && vol->nodes[fi].kind != scans[i].kind) {
            hostfs_deactivate_node(vol, fi);
            fi = -1;
        }
        if (fi < 0) {
            if (hostfs_add_node_from_scan(vol, parent_index, &scans[i]) != A2_OK) {
                continue;
            }
            fi = hostfs_find_by_basename(vol, parent_index, scans[i].basename);
            if (fi < 0) {
                fi = hostfs_find_by_name(vol, parent_index, scans[i].prodos);
            }
            added = fi >= 0;
        }
        if (fi < 0) {
            continue;
        }
        hostfs_marks_set(&matched, fi);
        vol->nodes[fi].parent_index = parent_index;
        hostfs_node_set_basename(vol, fi, scans[i].basename);
        vol->nodes[fi].host_mtime = (int64_t)scans[i].mtime;

        if (scans[i].kind == HOSTFS_KIND_DIR) {
            snprintf(
                vol->nodes[fi].prodos_name, sizeof(vol->nodes[fi].prodos_name), "%s",
                scans[i].prodos);
            vol->nodes[fi].name_len = (uint8_t)strlen(vol->nodes[fi].prodos_name);
            if ((recursive || added) && depth < HOSTFS_MAX_DEPTH &&
                hostfs_node_host_path(vol, fi, child_path, sizeof(child_path)) == A2_OK &&
                hostfs_rescan_directory_at(
                    vol, fi, depth + 1, true, targeted && added,
                    preserve_guest_eof) != A2_OK) {
                rc = A2_ERR;
            }
        } else if (hostfs_update_file_from_scan(
                       vol, fi, &scans[i], preserve_guest_eof) != A2_OK) {
            rc = A2_ERR;
        }
    }

    /* Only direct children are candidates here. A removed directory cascades
       through its descendants; untouched sibling subtrees are never scanned. */
    for (i = 0; i < vol->node_count; ++i) {
        if (vol->nodes[i].active && vol->nodes[i].parent_index == parent_index &&
            !hostfs_marks_get(&matched, i)) {
            hostfs_deactivate_node(vol, i);
        }
    }
    if (hostfs_rebuild_directory_order(
            vol, parent_index, scans, n, order_count > 0) != A2_OK) {
        rc = A2_ERR;
    }
    (void)hostfs_sync_bitmap_to_map(vol);
    hostfs_map_assert_consistent(vol);

done:
    hostfs_marks_free(&matched);
    free(scans);
    free(order_names);
    return rc;
}

int hostfs_rescan(hostfs_volume *vol)
{
    int rc;

    if (vol == NULL) {
        return A2_ERR;
    }
    if (vol->guest_write_depth > 0) {
        return A2_OK;
    }

    vol->internal_full_rescan_required = false;
    vol->dirty_parent_count = 0;
    vol->dirty_root = false;
    /* Stamp before the walk so a slow/failing rescan still rate-limits touches. */
    vol->last_refresh_ms = hostfs_now_ms();
    vol->full_rescans++;
    vol->watch_coverage_failed = false;
    rc = hostfs_rescan_directory_at(vol, -1, 0, true, false, false);
    (void)hostfs_patch_volume_file_count(vol);
    (void)hostfs_sync_bitmap_to_map(vol);
    (void)hostfs_persist_order_manifest(vol);
    hostfs_map_assert_consistent(vol);
    if (vol->watch_coverage_failed) {
        hostfs_enter_periodic_refresh(vol);
    }
    return rc;
}

void hostfs_bind_apple(hostfs_volume *vol, struct apple2 *m, int slot, int device)
{
    if (vol == NULL) {
        return;
    }
    vol->apple = m;
    vol->slot = slot;
    vol->device = device;
}

static bool hostfs_append_refresh_event(
    hostfs_refresh_event **events,
    int *count,
    int *cap,
    uint32_t flags,
    const char *relative_path)
{
    hostfs_refresh_event *grown;
    int i;
    int new_cap;

    if (!hostfs_refresh_path_valid(relative_path)) {
        return false;
    }
    for (i = 0; i < *count; ++i) {
        if (strcmp((*events)[i].relative_path, relative_path) == 0) {
            (*events)[i].flags |= flags;
            return true;
        }
    }
    if (*count >= *cap) {
        new_cap = *cap < 16 ? 16 : *cap * 2;
        grown = (hostfs_refresh_event *)realloc(
            *events, (size_t)new_cap * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        *events = grown;
        *cap = new_cap;
    }
    (*events)[*count].flags = flags;
    snprintf(
        (*events)[*count].relative_path,
        sizeof((*events)[*count].relative_path), "%s", relative_path);
    (*count)++;
    return true;
}

static int hostfs_refresh_file_node(hostfs_volume *vol, int file_index)
{
    hostfs_scan_ent scan;
    struct stat st;
    char path[HOSTFS_PATH_MAX];
    uint8_t *entry;

    if (file_index < 0 || file_index >= vol->node_count ||
        !vol->nodes[file_index].active ||
        vol->nodes[file_index].kind != HOSTFS_KIND_FILE ||
        hostfs_node_host_path(vol, file_index, path, sizeof(path)) != A2_OK) {
        return A2_ERR;
    }
    vol->targeted_file_stats++;
    if (stat(path, &st) != 0 || A2M_STAT_ISDIR(st.st_mode)) {
        return A2_ERR;
    }
    memset(&scan, 0, sizeof(scan));
    scan.kind = HOSTFS_KIND_FILE;
    snprintf(scan.basename, sizeof(scan.basename), "%s", hostfs_node_basename(vol, file_index));
    if (!hostfs_naps_parse_name(
            scan.basename, scan.prodos, sizeof(scan.prodos), &scan.type, &scan.aux)) {
        return A2_ERR;
    }
    scan.size = st.st_size < 0 ? 0u : (uint64_t)st.st_size;
    scan.mtime = st.st_mtime;
    if (hostfs_update_file_from_scan(vol, file_index, &scan, true) != A2_OK) {
        return A2_ERR;
    }
    entry = hostfs_find_dir_entry_slot(
        vol, vol->nodes[file_index].parent_index,
        vol->nodes[file_index].prodos_name, NULL);
    if (entry != NULL) {
        hostfs_fill_file_entry(
            entry, &vol->nodes[file_index],
            hostfs_parent_key(vol, vol->nodes[file_index].parent_index));
    }
    (void)hostfs_sync_bitmap_to_map(vol);
    hostfs_map_assert_consistent(vol);
    return A2_OK;
}

static void hostfs_clear_scheduled_directories(hostfs_volume *vol)
{
    vol->dirty_parent_count = 0;
    vol->dirty_root = false;
}

static int hostfs_recover_full_refresh(hostfs_volume *vol)
{
    int rc;

    hostfs_warn(vol, "%s: filesystem events were lost; performing a full refresh",
                vol->root_path);
    if (!vol->synthetic_refresh) {
        fs_watch_destroy(vol->watch);
        vol->watch = fs_watch_create(vol->root_path);
        vol->periodic_refresh = false;
        if (vol->watch == NULL) {
            hostfs_enter_periodic_refresh(vol);
        }
    }
    vol->synthetic_event_count = 0;
    hostfs_clear_scheduled_directories(vol);
    rc = hostfs_rescan(vol);
    return rc;
}

static int hostfs_process_refresh_events(hostfs_volume *vol, bool *out_did_work)
{
    hostfs_refresh_event *events = NULL;
    int event_count = 0;
    int event_cap = 0;
    int *files = NULL;
    int file_count = 0;
    int *parents = NULL;
    int parent_count = 0;
    int parent_cap = 0;
    bool root_dirty = false;
    bool need_full = vol->refresh_rescan_required;
    bool internal_full = vol->internal_full_rescan_required;
    int rename_count = 0;
    int i;
    int rc = A2_OK;

    *out_did_work = false;
    vol->refresh_rescan_required = false;
    vol->internal_full_rescan_required = false;
    if (vol->watch != NULL && fs_watch_take_rescan_required(vol->watch)) {
        need_full = true;
    }
    if (need_full) {
        *out_did_work = true;
        return hostfs_recover_full_refresh(vol);
    }
    if (internal_full) {
        vol->synthetic_event_count = 0;
        hostfs_clear_scheduled_directories(vol);
        *out_did_work = true;
        return hostfs_rescan(vol);
    }

    if (vol->watch != NULL) {
        fs_watch_event event;
        while (fs_watch_try_pop(vol->watch, &event)) {
            if (!hostfs_append_refresh_event(
                    &events, &event_count, &event_cap, event.flags,
                    event.relative_path)) {
                need_full = true;
                break;
            }
        }
    }
    for (i = 0; !need_full && i < vol->synthetic_event_count; ++i) {
        if (!hostfs_append_refresh_event(
                &events, &event_count, &event_cap,
                vol->synthetic_events[i].flags,
                vol->synthetic_events[i].relative_path)) {
            need_full = true;
        }
    }
    vol->synthetic_event_count = 0;
    if (need_full) {
        free(events);
        *out_did_work = true;
        return hostfs_recover_full_refresh(vol);
    }

    root_dirty = vol->dirty_root;
    for (i = 0; i < vol->dirty_parent_count; ++i) {
        if (!hostfs_int_list_add(
                &parents, &parent_count, &parent_cap,
                vol->dirty_parents[i])) {
            need_full = true;
            break;
        }
    }
    hostfs_clear_scheduled_directories(vol);

    if (need_full) {
        free(parents);
        free(events);
        *out_did_work = true;
        return hostfs_recover_full_refresh(vol);
    }

    if (event_count > 0) {
        files = (int *)malloc((size_t)event_count * sizeof(*files));
        if (files == NULL) {
            free(parents);
            free(events);
            *out_did_work = true;
            return hostfs_recover_full_refresh(vol);
        }
    }
    for (i = 0; i < event_count; ++i) {
        uint32_t structural =
            events[i].flags & (FS_WATCH_CREATE | FS_WATCH_REMOVE |
                               FS_WATCH_RENAME | FS_WATCH_DIRECTORY);
        int parent_index;
        int file_index;

        if ((events[i].flags & FS_WATCH_RENAME) != 0u) {
            rename_count++;
        }
        file_index = hostfs_find_by_relative_path(vol, events[i].relative_path);
        if (structural == 0u && file_index >= 0 &&
            vol->nodes[file_index].kind == HOSTFS_KIND_FILE &&
            hostfs_basename_cmp(
                hostfs_node_basename(vol, file_index), HOSTFS_ORDER_FILENAME) != 0) {
            files[file_count++] = file_index;
            continue;
        }
        if (!hostfs_parent_from_relative_path(
                vol, events[i].relative_path, &parent_index)) {
            need_full = true;
            break;
        }
        if (parent_index < 0) {
            root_dirty = true;
        } else if (!hostfs_int_list_add(
                       &parents, &parent_count, &parent_cap, parent_index)) {
            need_full = true;
            break;
        }
    }
    /* A lone rename path is not enough to prove that the old and new parents
       were both invalidated. Native backends either provide both or set loss. */
    if ((rename_count & 1) != 0) {
        need_full = true;
    }
    if (need_full) {
        free(parents);
        free(files);
        free(events);
        *out_did_work = true;
        return hostfs_recover_full_refresh(vol);
    }

    if (root_dirty &&
        hostfs_rescan_directory_at(
            vol, -1, 0, false, true, true) != A2_OK) {
        rc = A2_ERR;
    }
    for (i = 0; i < parent_count; ++i) {
        int parent_index = parents[i];
        if (parent_index < 0 || parent_index >= vol->node_count ||
            !vol->nodes[parent_index].active ||
            vol->nodes[parent_index].kind != HOSTFS_KIND_DIR) {
            rc = A2_ERR;
            continue;
        }
        if (hostfs_rescan_directory_at(
                vol, parent_index, hostfs_parent_depth(vol, parent_index), false, true,
                true) !=
            A2_OK) {
            rc = A2_ERR;
        }
    }
    for (i = 0; i < file_count; ++i) {
        int fi = files[i];
        int parent_index;
        if (fi < 0 || fi >= vol->node_count || !vol->nodes[fi].active) {
            continue;
        }
        parent_index = vol->nodes[fi].parent_index;
        if ((parent_index < 0 && root_dirty) ||
            (parent_index >= 0 &&
             hostfs_int_list_contains(parents, parent_count, parent_index))) {
            continue;
        }
        if (hostfs_refresh_file_node(vol, fi) != A2_OK) {
            if (hostfs_rescan_directory_at(
                    vol, parent_index, hostfs_parent_depth(vol, parent_index),
                    false, true, true) != A2_OK) {
                rc = A2_ERR;
            }
        }
    }
    *out_did_work = root_dirty || parent_count > 0 || file_count > 0;
    free(parents);
    free(files);
    free(events);
    return rc;
}

void hostfs_maybe_refresh(hostfs_volume *vol)
{
    uint64_t now;
    bool did_work = false;

    if (vol == NULL || vol->guest_write_depth > 0) {
        return;
    }
    if (vol->apple != NULL && vol->apple->replay_sealed) {
        return;
    }
    (void)hostfs_process_refresh_events(vol, &did_work);
    if (!did_work && vol->periodic_refresh) {
        now = hostfs_now_ms();
        if (vol->last_refresh_ms == 0u ||
            now - vol->last_refresh_ms >= (uint64_t)HOSTFS_REFRESH_PERIOD_MS) {
            (void)hostfs_rescan(vol);
            did_work = true;
        }
    }
    if (did_work) {
        vol->host_directory_changed = 1;
    }
    if (vol->host_directory_changed != 0) {
        vol->host_directory_changed = 0;
        apple2_note_media_event(
            vol->apple,
            vol->slot,
            vol->device,
            APPLE2_MEDIA_EVENT_HOST_DIRECTORY);
    }
}

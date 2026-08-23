#include "app_options.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "argparse.h"
#include "config.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define A2M_DEFAULT_INI "a2m.ini"
#define A2M_DEFAULT_VIDEO_STANDARD "NTSC"
#define A2M_DEFAULT_KEYBOARD_JOYSTICK_LAYOUT "numpad"
#define A2M_DEFAULT_SCROLL_WHEEL_LINES 3
#define A2M_DEFAULT_CRT_SCANLINE_STRENGTH 35
#define A2M_DEFAULT_CRT_CURVATURE_AMOUNT 30
#define A2M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT 0.62f
#define A2M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM 0.58f
#define A2M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC 0.55f
#define A2M_DEFAULT_HISTORY_MEMORY_MB 256
#define A2M_DEFAULT_FRAME_RING_MEMORY_MB 128
#define A2M_DEFAULT_TIMEMACHINE_MEMORY_MB 128
#define A2M_SYSTEM_ROM_SIZE 16384
#define A2M_BASIC_ROM_SIZE 8192
#define A2M_KERNAL_ROM_SIZE 8192
#define A2M_CHARACTER_ROM_SIZE 4096

#if defined(_WIN32)
#define A2M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#define a2m_getcwd _getcwd
#else
#define A2M_STAT_ISREG(mode) S_ISREG(mode)
#define a2m_getcwd getcwd
#endif

static void normalize_path_separators(char *path)
{
    char *cursor;

    if (path == NULL) {
        return;
    }
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }
}

static char *copy_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

static bool replace_string(char **target, const char *value)
{
    char *copy;

    copy = copy_string(value);
    if (copy == NULL && value != NULL) {
        return false;
    }

    free(*target);
    *target = copy;
    return true;
}

bool app_options_set_string(char **target, const char *value)
{
    return replace_string(target, value);
}

const char *app_model_label(int apple_model)
{
    return apple_model == 1 ? "][+" : "//e Enhanced";
}

bool app_model_from_string(const char *s, int *out_model)
{
    if (s == NULL || out_model == NULL) {
        return false;
    }
    if (strcasecmp(s, "plus") == 0 || strcasecmp(s, "ii+") == 0 ||
        strcasecmp(s, "2plus") == 0 || strcasecmp(s, "][+") == 0) {
        *out_model = 1;
        return true;
    }
    if (strcasecmp(s, "enh") == 0 || strcasecmp(s, "enhanced") == 0 ||
        strcasecmp(s, "iie") == 0 || strcasecmp(s, "//e") == 0 ||
        strcasecmp(s, "e") == 0) {
        *out_model = 0;
        return true;
    }
    return false;
}

const char *app_slot_card_name(app_slot_card_type type)
{
    switch (type) {
        case APP_SLOT_CARD_EMPTY:        return "empty";
        case APP_SLOT_CARD_DISKII:       return "diskii";
        case APP_SLOT_CARD_SMARTPORT:    return "smartport";
        case APP_SLOT_CARD_MOCKINGBOARD: return "mockingboard";
        default:                         return "empty";
    }
}

bool app_slot_card_from_string(const char *s, app_slot_card_type *out_type)
{
    app_slot_card_type type;

    if (s == NULL || out_type == NULL) {
        return false;
    }
    if (strcasecmp(s, "empty") == 0 || strcasecmp(s, "off") == 0) {
        type = APP_SLOT_CARD_EMPTY;
    } else if (strcasecmp(s, "diskii") == 0 || strcasecmp(s, "disk ii") == 0) {
        type = APP_SLOT_CARD_DISKII;
    } else if (strcasecmp(s, "smartport") == 0) {
        type = APP_SLOT_CARD_SMARTPORT;
    } else if (strcasecmp(s, "mockingboard") == 0) {
        type = APP_SLOT_CARD_MOCKINGBOARD;
    } else {
        return false;
    }
    *out_type = type;
    return true;
}

bool app_options_set_slot_card(app_options *options, int slot, app_slot_card_type type)
{
    int i;

    if (options == NULL || slot < 1 || slot > 7 ||
        type < APP_SLOT_CARD_EMPTY || type > APP_SLOT_CARD_MOCKINGBOARD) {
        return false;
    }

    if (type == APP_SLOT_CARD_MOCKINGBOARD) {
        for (i = 1; i <= 7; ++i) {
            if (i != slot && options->slot_cards[i] == APP_SLOT_CARD_MOCKINGBOARD) {
                options->slot_cards[i] = APP_SLOT_CARD_EMPTY;
            }
        }
        options->mb_slot = slot;
    } else if (options->slot_cards[slot] == APP_SLOT_CARD_MOCKINGBOARD &&
               options->mb_slot == slot) {
        options->mb_slot = 0;
    }
    options->slot_cards[slot] = type;
    return true;
}

void app_options_reconcile_slot_cards(app_options *options)
{
    int i;

    if (options == NULL) {
        return;
    }
    for (i = 0; i < options->diskii_count;) {
        int slot = options->diskii[i].slot;
        if (slot < 1 || slot > 7 || options->slot_cards[slot] != APP_SLOT_CARD_DISKII) {
            int j;
            free(options->diskii[i].path);
            for (j = i; j < options->diskii_count - 1; ++j) {
                options->diskii[j] = options->diskii[j + 1];
            }
            options->diskii_count--;
            options->diskii[options->diskii_count].path = NULL;
            continue;
        }
        ++i;
    }
    for (i = 0; i < options->smartport_count;) {
        int slot = options->smartport[i].slot;
        if (slot < 1 || slot > 7 || options->slot_cards[slot] != APP_SLOT_CARD_SMARTPORT) {
            int j;
            free(options->smartport[i].path);
            for (j = i; j < options->smartport_count - 1; ++j) {
                options->smartport[j] = options->smartport[j + 1];
            }
            options->smartport_count--;
            options->smartport[options->smartport_count].path = NULL;
            continue;
        }
        ++i;
    }
    app_options_sync_convenience_paths(options);
    app_options_rebuild_diskii_queues(options);
}

bool app_options_parse_slot_unit_key(const char *key, int *out_slot, int *out_unit)
{
    int slot;
    int unit;

    if (key == NULL || out_slot == NULL || out_unit == NULL) {
        return false;
    }
    if (strlen(key) != 4) {
        return false;
    }
    if ((key[0] != 's' && key[0] != 'S') || (key[2] != 'd' && key[2] != 'D')) {
        return false;
    }
    if (!isdigit((unsigned char)key[1]) || !isdigit((unsigned char)key[3])) {
        return false;
    }
    slot = key[1] - '0';
    unit = key[3] - '0';
    if (slot < 1 || slot > 7 || unit < 0 || unit > 1) {
        return false;
    }
    *out_slot = slot;
    *out_unit = unit;
    return true;
}

/* Remove every Disk II mount entry for slot+drive (used by convenience replace). */
static void remove_diskii_mounts_for(app_options *options, int slot, int drive)
{
    int i = 0;

    if (options == NULL) {
        return;
    }
    while (i < options->diskii_count) {
        if (options->diskii[i].slot == slot && options->diskii[i].drive == drive) {
            free(options->diskii[i].path);
            options->diskii[i].path = NULL;
            if (i < options->diskii_count - 1) {
                options->diskii[i] = options->diskii[options->diskii_count - 1];
                options->diskii[options->diskii_count - 1].path = NULL;
            }
            options->diskii_count--;
            continue;
        }
        i++;
    }
}

/*
 * Append a Disk II image for slot+drive. Multiple entries for the same
 * slot+drive form the multi-image queue (order preserved). CLI: repeat
 * -d s6d0=a.nib -d s6d0=b.nib.
 */
static bool add_diskii_mount(app_options *options, int slot, int drive, const char *path)
{
    if (options == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    if (slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        fprintf(stderr, "a2m: Disk II mount slot must be 1..7, drive 0..1\n");
        return false;
    }
    if (options->diskii_count >= APP_OPTIONS_MAX_DISKII) {
        fprintf(stderr, "a2m: too many Disk II mounts (max %d)\n", APP_OPTIONS_MAX_DISKII);
        return false;
    }
    options->diskii[options->diskii_count].slot = slot;
    options->diskii[options->diskii_count].drive = drive;
    options->diskii[options->diskii_count].path = NULL;
    if (!replace_string(&options->diskii[options->diskii_count].path, path)) {
        return false;
    }
    options->diskii_count++;
    return true;
}

static bool add_diskii_mounts_from_list(
    app_options *options,
    int slot,
    int drive,
    const char *spec);
static bool format_diskii_mount_list(
    const app_options *options,
    int slot,
    int drive,
    char *out,
    size_t out_size);

static bool add_smartport_mount(app_options *options, int slot, int unit, const char *path)
{
    int i;

    if (options == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    if (slot < 1 || slot > 7 || unit < 0 || unit > 1) {
        fprintf(stderr, "a2m: SmartPort mount slot must be 1..7, unit 0..1\n");
        return false;
    }
    for (i = 0; i < options->smartport_count; i++) {
        if (options->smartport[i].slot == slot && options->smartport[i].unit == unit) {
            return replace_string(&options->smartport[i].path, path);
        }
    }
    if (options->smartport_count >= APP_OPTIONS_MAX_SMARTPORT) {
        fprintf(
            stderr,
            "a2m: too many SmartPort mounts (max %d)\n",
            APP_OPTIONS_MAX_SMARTPORT);
        return false;
    }
    options->smartport[options->smartport_count].slot = slot;
    options->smartport[options->smartport_count].unit = unit;
    options->smartport[options->smartport_count].path = NULL;
    if (!replace_string(&options->smartport[options->smartport_count].path, path)) {
        return false;
    }
    options->smartport_count++;
    return true;
}

bool app_options_diskii_append_path(
    app_options *options, int slot, int drive, const char *path)
{
    app_disk_slot *queue;
    int added_index;
    if (!add_diskii_mount(options, slot, drive, path)) {
        return false;
    }
    added_index = options->diskii_count - 1;
    queue = app_options_diskii_queue(options, slot, drive);
    if (queue != NULL && app_disk_slot_append(queue, path)) {
        return true;
    }
    free(options->diskii[added_index].path);
    options->diskii[added_index].path = NULL;
    options->diskii_count--;
    return false;
}

bool app_options_diskii_eject_current(app_options *options, int slot, int drive)
{
    app_disk_slot *queue;
    int target;
    int seen = 0;
    int i;

    queue = app_options_diskii_queue(options, slot, drive);
    if (queue == NULL || queue->count <= 0) {
        return true;
    }
    target = queue->current;
    for (i = 0; i < options->diskii_count; ++i) {
        if (options->diskii[i].slot != slot || options->diskii[i].drive != drive) {
            continue;
        }
        if (seen++ == target) {
            int j;
            free(options->diskii[i].path);
            for (j = i; j < options->diskii_count - 1; ++j) {
                options->diskii[j] = options->diskii[j + 1];
            }
            options->diskii_count--;
            options->diskii[options->diskii_count].path = NULL;
            (void)app_disk_slot_eject_current(queue);
            return true;
        }
    }
    return false;
}

bool app_options_smartport_set_path(
    app_options *options, int slot, int device, const char *path)
{
    return add_smartport_mount(options, slot, device, path);
}

void app_options_smartport_clear_path(app_options *options, int slot, int device)
{
    int i;
    if (options == NULL) {
        return;
    }
    for (i = 0; i < options->smartport_count; ++i) {
        if (options->smartport[i].slot == slot && options->smartport[i].unit == device) {
            int j;
            free(options->smartport[i].path);
            for (j = i; j < options->smartport_count - 1; ++j) {
                options->smartport[j] = options->smartport[j + 1];
            }
            options->smartport_count--;
            options->smartport[options->smartport_count].path = NULL;
            break;
        }
    }
}

/* Accept "path", "s6d0=path", or "s6d0:path". Bare path → defaults. */
static bool parse_mount_spec(
    const char *spec,
    int default_slot,
    int default_unit,
    int *out_slot,
    int *out_unit,
    const char **out_path)
{
    const char *eq;
    char key[8];
    size_t key_len;

    if (spec == NULL || spec[0] == '\0' || out_slot == NULL || out_unit == NULL ||
        out_path == NULL) {
        return false;
    }

    eq = strchr(spec, '=');
    if (eq == NULL) {
        eq = strchr(spec, ':');
        /* Only treat colon as separator for short sNdN: keys, not Windows paths. */
        if (eq != NULL && (size_t)(eq - spec) != 4) {
            eq = NULL;
        }
    }

    if (eq == NULL) {
        *out_slot = default_slot;
        *out_unit = default_unit;
        *out_path = spec;
        return true;
    }

    key_len = (size_t)(eq - spec);
    if (key_len >= sizeof(key) || key_len == 0) {
        return false;
    }
    memcpy(key, spec, key_len);
    key[key_len] = '\0';
    if (!app_options_parse_slot_unit_key(key, out_slot, out_unit)) {
        return false;
    }
    *out_path = eq + 1;
    return (*out_path)[0] != '\0';
}

static bool add_diskii_spec(app_options *options, const char *spec)
{
    int slot = 6;
    int drive = 0;
    const char *path = NULL;

    if (!parse_mount_spec(spec, 6, 0, &slot, &drive, &path)) {
        fprintf(stderr, "a2m: invalid --disk mount: %s (use path or s6d0=path)\n", spec);
        return false;
    }
    return add_diskii_mount(options, slot, drive, path);
}

static bool add_smartport_spec(app_options *options, const char *spec)
{
    int slot = 7;
    int unit = 0;
    const char *path = NULL;

    if (!parse_mount_spec(spec, 7, 0, &slot, &unit, &path)) {
        fprintf(stderr, "a2m: invalid --hd mount: %s (use path or s7d0=path)\n", spec);
        return false;
    }
    return add_smartport_mount(options, slot, unit, path);
}

static const char *find_smartport_path(const app_options *options, int slot, int unit)
{
    int i;
    if (options == NULL) {
        return NULL;
    }
    for (i = 0; i < options->smartport_count; i++) {
        if (options->smartport[i].slot == slot && options->smartport[i].unit == unit) {
            return options->smartport[i].path;
        }
    }
    return NULL;
}

void app_options_sync_convenience_paths(app_options *options)
{
    char list[8192];
    const char *p;
    if (options == NULL) {
        return;
    }
    if (format_diskii_mount_list(options, 6, 0, list, sizeof(list))) {
        (void)replace_string(&options->disk_s6d0, list);
    }
    if (format_diskii_mount_list(options, 6, 1, list, sizeof(list))) {
        (void)replace_string(&options->disk_s6d1, list);
    }
    p = find_smartport_path(options, 7, 0);
    (void)replace_string(&options->hd_s7d0, p != NULL ? p : "");
    p = find_smartport_path(options, 5, 0);
    (void)replace_string(&options->hd_s5d0, p != NULL ? p : "");
}

bool app_options_apply_convenience_paths(app_options *options)
{
    if (options == NULL) {
        return false;
    }
    /* Convenience buffers replace the whole queue for that drive. Values may
       be a single path or a comma-separated multi-image list. */
    if (options->disk_s6d0 != NULL && options->disk_s6d0[0] != '\0') {
        remove_diskii_mounts_for(options, 6, 0);
        if (!add_diskii_mounts_from_list(options, 6, 0, options->disk_s6d0)) {
            return false;
        }
    }
    if (options->disk_s6d1 != NULL && options->disk_s6d1[0] != '\0') {
        remove_diskii_mounts_for(options, 6, 1);
        if (!add_diskii_mounts_from_list(options, 6, 1, options->disk_s6d1)) {
            return false;
        }
    }
    if (options->hd_s7d0 != NULL && options->hd_s7d0[0] != '\0') {
        if (!add_smartport_mount(options, 7, 0, options->hd_s7d0)) {
            return false;
        }
    }
    if (options->hd_s5d0 != NULL && options->hd_s5d0[0] != '\0') {
        if (!add_smartport_mount(options, 5, 0, options->hd_s5d0)) {
            return false;
        }
    }
    app_options_rebuild_diskii_queues(options);
    return true;
}

static bool section_is(const char *section, const char *name)
{
    return section != NULL && name != NULL && strcasecmp(section, name) == 0;
}

static bool path_is_absolute(const char *path)
{
    return path != NULL &&
        (path[0] == '/'
#if defined(_WIN32)
         || path[0] == '\\' ||
         (isalpha((unsigned char)path[0]) && path[1] == ':' &&
          (path[2] == '/' || path[2] == '\\'))
#endif
        );
}

static bool copy_path(char *out, size_t out_size, const char *path)
{
    int written;

    if (out == NULL || out_size == 0 || path == NULL) {
        return false;
    }

    written = snprintf(out, out_size, "%s", path);
    if (written < 0 || (size_t)written >= out_size) {
        return false;
    }
    normalize_path_separators(out);
    return true;
}

static bool join_path_buffer(char *out, size_t out_size, const char *dir, const char *path)
{
    int written;

    if (out == NULL || out_size == 0 || dir == NULL || path == NULL) {
        return false;
    }
    if (dir[0] == '\0' || strcmp(dir, ".") == 0) {
        written = snprintf(out, out_size, "%s", path);
    } else if (strcmp(dir, "/") == 0) {
        written = snprintf(out, out_size, "/%s", path);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, path);
    }

    return written >= 0 && (size_t)written < out_size;
}

static bool copy_resolved_or_original(char *out, size_t out_size, const char *path)
{
    char resolved[PATH_MAX];

#if defined(_WIN32)
    if (_fullpath(resolved, path, sizeof(resolved)) != NULL) {
        return copy_path(out, out_size, resolved);
    }
#else
    if (realpath(path, resolved) != NULL) {
        return copy_path(out, out_size, resolved);
    }
#endif
    return copy_path(out, out_size, path);
}

static bool ini_directory_absolute(const app_options *options, char *out, size_t out_size)
{
    char cwd[PATH_MAX];
    char ini_copy[PATH_MAX];
    char joined[PATH_MAX];
    char *slash;

    if (out == NULL || out_size == 0) {
        return false;
    }
    if (a2m_getcwd(cwd, sizeof(cwd)) == NULL) {
        return copy_path(out, out_size, ".");
    }
    normalize_path_separators(cwd);
    if (options == NULL || options->ini_path == NULL || options->ini_path[0] == '\0') {
        return copy_path(out, out_size, cwd);
    }

    if (!copy_path(ini_copy, sizeof(ini_copy), options->ini_path)) {
        return false;
    }

    slash = strrchr(ini_copy, '/');
    if (slash == NULL) {
        return copy_path(out, out_size, cwd);
    }
    if (slash == ini_copy) {
        return copy_path(out, out_size, "/");
    }
    *slash = '\0';
    if (path_is_absolute(ini_copy)) {
        return copy_resolved_or_original(out, out_size, ini_copy);
    }
    if (!join_path_buffer(joined, sizeof(joined), cwd, ini_copy)) {
        return false;
    }
    return copy_resolved_or_original(out, out_size, joined);
}

static bool path_absolute_from_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    char ini_dir[PATH_MAX];
    char joined[PATH_MAX];

    if (path == NULL || path[0] == '\0') {
        return copy_path(out, out_size, "");
    }
    if (path_is_absolute(path)) {
        return copy_resolved_or_original(out, out_size, path);
    }
    if (!ini_directory_absolute(options, ini_dir, sizeof(ini_dir))) {
        return false;
    }
    if (!join_path_buffer(joined, sizeof(joined), ini_dir, path)) {
        return false;
    }
    return copy_resolved_or_original(out, out_size, joined);
}

bool app_options_path_absolute_from_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    return path_absolute_from_ini(options, path, out, out_size);
}

static size_t path_component_length(const char *path)
{
    const char *slash;

    slash = strchr(path, '/');
    return slash != NULL ? (size_t)(slash - path) : strlen(path);
}

static bool append_relative_up(char *out, size_t out_size, size_t *used)
{
    int written;

    written = snprintf(out + *used, out_size - *used, "%s..", *used > 0 ? "/" : "");
    if (written < 0 || (size_t)written >= out_size - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool relative_path_from_dir(
    const char *base_dir,
    const char *abs_path,
    char *out,
    size_t out_size)
{
    const char *base_cursor;
    const char *path_cursor;
    const char *base_remainder;
    const char *path_remainder;
    size_t used = 0;

    if (!path_is_absolute(base_dir) || !path_is_absolute(abs_path)) {
        return copy_path(out, out_size, abs_path);
    }

    base_cursor = base_dir;
    path_cursor = abs_path;
    while (*base_cursor == '/' && *path_cursor == '/') {
        base_cursor++;
        path_cursor++;
    }

    base_remainder = base_cursor;
    path_remainder = path_cursor;
    while (*base_cursor != '\0' && *path_cursor != '\0') {
        size_t base_len = path_component_length(base_cursor);
        size_t path_len = path_component_length(path_cursor);

        if (base_len != path_len || strncmp(base_cursor, path_cursor, base_len) != 0) {
            break;
        }

        base_cursor += base_len;
        path_cursor += path_len;
        if (*base_cursor == '/') {
            base_cursor++;
        }
        if (*path_cursor == '/') {
            path_cursor++;
        }
        base_remainder = base_cursor;
        path_remainder = path_cursor;
    }

    while (*base_remainder != '\0') {
        size_t component_len = path_component_length(base_remainder);
        if (component_len > 0 && !append_relative_up(out, out_size, &used)) {
            return false;
        }
        base_remainder += component_len;
        if (*base_remainder == '/') {
            base_remainder++;
        }
    }

    if (*path_remainder != '\0') {
        int written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "/" : "", path_remainder);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
    } else if (used == 0) {
        return copy_path(out, out_size, ".");
    }

    return true;
}

bool app_options_path_relative_to_ini(
    const app_options *options,
    const char *path,
    char *out,
    size_t out_size)
{
    char ini_dir[PATH_MAX];
    char resolved[PATH_MAX];

    if (path == NULL || path[0] == '\0') {
        return copy_path(out, out_size, "");
    }
    if (!path_is_absolute(path)) {
        return copy_path(out, out_size, path);
    }
    if (!ini_directory_absolute(options, ini_dir, sizeof(ini_dir))) {
        return false;
    }
#if defined(_WIN32)
    if (_fullpath(resolved, path, sizeof(resolved)) != NULL) {
        normalize_path_separators(resolved);
        return relative_path_from_dir(ini_dir, resolved, out, out_size);
    }
#else
    if (realpath(path, resolved) != NULL) {
        return relative_path_from_dir(ini_dir, resolved, out, out_size);
    }
#endif
    return relative_path_from_dir(ini_dir, path, out, out_size);
}

static bool transform_symbol_files(
    const app_options *options,
    const char *symbol_files,
    bool absolute,
    char *out,
    size_t out_size)
{
    const char *cursor;
    size_t used = 0;

    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    cursor = symbol_files != NULL ? symbol_files : "";
    while (*cursor != '\0') {
        const char *start;
        const char *end;
        char path[PATH_MAX];
        char transformed[PATH_MAX];
        size_t length;
        int written;

        while (*cursor == ',' || isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }

        length = (size_t)(end - start);
        if (length == 0) {
            continue;
        }
        if (length >= sizeof(path)) {
            return false;
        }
        memcpy(path, start, length);
        path[length] = '\0';

        if (absolute) {
            if (!path_absolute_from_ini(options, path, transformed, sizeof(transformed))) {
                return false;
            }
        } else {
            if (!app_options_path_relative_to_ini(options, path, transformed, sizeof(transformed))) {
                return false;
            }
        }

        written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "," : "", transformed);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }

    return true;
}

bool app_options_symbol_files_absolute(
    const app_options *options,
    char *out,
    size_t out_size)
{
    if (options == NULL) {
        return false;
    }
    return transform_symbol_files(options, options->symbol_files, true, out, out_size);
}

static bool string_equal_ignore_case(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return false;
        }
    }

    return *a == '\0' && *b == '\0';
}

static bool rom_candidate_name_matches(const char *filename, const char *rom_name)
{
    char stem[256];
    const char *dot;
    size_t length;

    if (string_equal_ignore_case(filename, rom_name)) {
        return true;
    }

    dot = strrchr(filename, '.');
    if (dot == NULL || dot == filename) {
        return false;
    }

    length = (size_t)(dot - filename);
    if (length >= sizeof(stem)) {
        return false;
    }

    memcpy(stem, filename, length);
    stem[length] = '\0';
    return string_equal_ignore_case(stem, rom_name);
}

static bool path_has_size(const char *path, size_t expected_size)
{
    struct stat st;

    if (stat(path, &st) != 0 || !A2M_STAT_ISREG(st.st_mode)) {
        return false;
    }

    return st.st_size >= 0 && (size_t)st.st_size == expected_size;
}

static bool join_path(char *out, size_t out_size, const char *dir, const char *filename)
{
    int written;

    if (strcmp(dir, ".") == 0) {
        written = snprintf(out, out_size, "%s", filename);
    } else {
        written = snprintf(out, out_size, "%s/%s", dir, filename);
    }

    return written >= 0 && (size_t)written < out_size;
}

static bool discover_rom_path(
    const char *dir,
    const char *rom_name,
    size_t expected_size,
    char **target)
{
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char search_path[1024];
    char path[1024];
#else
    DIR *handle;
    struct dirent *entry;
    char path[1024];
#endif

    if (*target != NULL) {
        return true;
    }

#if defined(_WIN32)
    if (!join_path(search_path, sizeof(search_path), dir, "*")) {
        return true;
    }
    handle = FindFirstFileA(search_path, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return true;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            !rom_candidate_name_matches(data.cFileName, rom_name)) {
            continue;
        }
        if (!join_path(path, sizeof(path), dir, data.cFileName)) {
            continue;
        }
        if (!path_has_size(path, expected_size)) {
            continue;
        }

        FindClose(handle);
        return replace_string(target, path);
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
#else
    handle = opendir(dir);
    if (handle == NULL) {
        return true;
    }

    while ((entry = readdir(handle)) != NULL) {
        if (!rom_candidate_name_matches(entry->d_name, rom_name)) {
            continue;
        }
        if (!join_path(path, sizeof(path), dir, entry->d_name)) {
            continue;
        }
        if (!path_has_size(path, expected_size)) {
            continue;
        }

        closedir(handle);
        return replace_string(target, path);
    }

    closedir(handle);
#endif
    return true;
}

static float config_get_float(config *cfg, const char *section, const char *key, float default_value)
{
    const char *value;
    char *end;
    float parsed;

    value = config_get(cfg, section, key);
    if (value == NULL) {
        return default_value;
    }

    parsed = strtof(value, &end);
    if (end == value || *end != '\0') {
        return default_value;
    }

    return parsed;
}

static void config_set_float(config *cfg, const char *section, const char *key, float value)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%.6g", value);
    config_set(cfg, section, key, buffer);
}

/* --- disk slot helpers --------------------------------------------------- */

static void disk_slot_free(app_disk_slot *slot)
{
    int i;

    for (i = 0; i < slot->count; ++i) {
        free(slot->paths[i]);
    }
    free(slot->paths);
    free(slot->writable);
    slot->paths = NULL;
    slot->writable = NULL;
    slot->count = 0;
    slot->current = 0;
}

static bool disk_slot_append(app_disk_slot *slot, const char *path)
{
    char **grown;
    bool *grown_writable;
    char *copy;

    copy = copy_string(path);
    if (copy == NULL) {
        return false;
    }

    grown = (char **)realloc(slot->paths, (size_t)(slot->count + 1) * sizeof(char *));
    if (grown == NULL) {
        free(copy);
        return false;
    }
    slot->paths = grown;

    grown_writable = (bool *)realloc(slot->writable, (size_t)(slot->count + 1) * sizeof(bool));
    if (grown_writable == NULL) {
        free(copy);
        return false;
    }

    grown[slot->count] = copy;
    slot->writable = grown_writable;
    slot->writable[slot->count] = false;
    slot->count++;
    return true;
}

static void disk_slot_parse_writable_list(app_disk_slot *slot, const char *spec)
{
    const char *cursor = spec;
    int index = 0;

    if (slot == NULL || spec == NULL) {
        return;
    }

    while (*cursor != '\0' && index < slot->count) {
        const char *start;
        const char *end;
        size_t len;

        while (*cursor == ' ') {
            cursor++;
        }
        start = cursor;
        while (*cursor != '\0' && *cursor != ',') {
            cursor++;
        }
        end = cursor;
        while (end > start && end[-1] == ' ') {
            end--;
        }
        len = (size_t)(end - start);
        slot->writable[index] =
            (len == 1 && start[0] == '1') ||
            (len == 4 && strncmp(start, "true", 4) == 0) ||
            (len == 2 && strncmp(start, "rw", 2) == 0);
        index++;
        if (*cursor == ',') {
            cursor++;
        }
    }
}

static bool disk_slot_format_writable_list(
    const app_disk_slot *slot,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int j;

    if (slot == NULL || out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    for (j = 0; j < slot->count; ++j) {
        int written = snprintf(
            out + used,
            out_size - used,
            "%s%d",
            used > 0 ? "," : "",
            slot->writable[j] ? 1 : 0);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }
    return true;
}

/*
 * Consume the next comma-separated path from *cursor into out.
 * Quoted tokens ("..." or '...') may contain commas; surrounding quotes
 * are stripped. Returns false at end of string. Empty tokens are skipped
 * by the caller when out is empty.
 */
static bool disk_slot_next_path(const char **cursor, char *out, size_t out_size)
{
    const char *s;
    size_t n = 0;

    if (cursor == NULL || *cursor == NULL || out == NULL || out_size == 0) {
        return false;
    }
    s = *cursor;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        *cursor = s;
        out[0] = '\0';
        return false;
    }

    if (*s == '"' || *s == '\'') {
        char quote = *s++;
        while (*s != '\0' && *s != quote) {
            if (n + 1u >= out_size) {
                out[0] = '\0';
                return false;
            }
            out[n++] = *s++;
        }
        if (*s == quote) {
            s++;
        }
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == ',') {
            s++;
        }
    } else {
        while (*s != '\0' && *s != ',') {
            if (n + 1u >= out_size) {
                out[0] = '\0';
                return false;
            }
            out[n++] = *s++;
        }
        while (n > 0u && isspace((unsigned char)out[n - 1u])) {
            n--;
        }
        if (*s == ',') {
            s++;
        }
    }

    out[n] = '\0';
    *cursor = s;
    return true;
}

/*
 * Parse a comma-separated list of paths into slot (replacing any prior
 * contents).  When resolve_options is non-NULL each path is resolved
 * relative to the INI directory; otherwise paths are kept as-is.
 */
static bool disk_slot_parse_list(
    app_disk_slot *slot,
    const app_options *resolve_options,
    const char *spec)
{
    const char *cursor = spec;

    disk_slot_free(slot);
    if (spec == NULL) {
        return true;
    }

    while (*cursor != '\0') {
        char path[PATH_MAX];

        if (!disk_slot_next_path(&cursor, path, sizeof(path))) {
            break;
        }
        if (path[0] == '\0') {
            continue;
        }

        if (resolve_options != NULL) {
            char abs_path[PATH_MAX];
            if (path_absolute_from_ini(resolve_options, path, abs_path, sizeof(abs_path))) {
                if (!disk_slot_append(slot, abs_path)) {
                    return false;
                }
            } else if (!disk_slot_append(slot, path)) {
                return false;
            }
        } else if (!disk_slot_append(slot, path)) {
            return false;
        }
    }

    return true;
}

/* Append each comma-separated path in spec as a Disk II mount. */
static bool add_diskii_mounts_from_list(
    app_options *options,
    int slot,
    int drive,
    const char *spec)
{
    app_disk_slot parsed;
    int i;
    bool ok = true;

    memset(&parsed, 0, sizeof(parsed));
    /* INI (and convenience) media paths are relative to the INI directory. */
    if (!disk_slot_parse_list(&parsed, options, spec)) {
        disk_slot_free(&parsed);
        return false;
    }
    for (i = 0; i < parsed.count; ++i) {
        if (!add_diskii_mount(options, slot, drive, parsed.paths[i])) {
            ok = false;
            break;
        }
    }
    disk_slot_free(&parsed);
    return ok;
}

/* Join diskii[] paths for slot+drive; rewrite each as INI-relative when possible. */
static bool format_diskii_mount_list(
    const app_options *options,
    int slot,
    int drive,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int i;

    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (options == NULL) {
        return true;
    }
    for (i = 0; i < options->diskii_count; ++i) {
        const char *path;
        char rel[PATH_MAX];
        int written;

        if (options->diskii[i].slot != slot || options->diskii[i].drive != drive) {
            continue;
        }
        path = options->diskii[i].path;
        if (path == NULL || path[0] == '\0') {
            continue;
        }
        if (!app_options_path_relative_to_ini(options, path, rel, sizeof(rel))) {
            if (!copy_path(rel, sizeof(rel), path)) {
                return false;
            }
        }
        written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "," : "", rel);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }
    return true;
}

/*
 * Write slot paths as a comma-separated string into out, converting each
 * absolute path to be relative to the INI directory.
 */
static bool disk_slot_format_list(
    const app_disk_slot *slot,
    const app_options *options,
    char *out,
    size_t out_size)
{
    size_t used = 0;
    int j;

    if (out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';

    for (j = 0; j < slot->count; ++j) {
        char rel[PATH_MAX];
        int written;

        if (!app_options_path_relative_to_ini(options, slot->paths[j], rel, sizeof(rel))) {
            if (!copy_path(rel, sizeof(rel), slot->paths[j])) {
                return false;
            }
        }

        written = snprintf(out + used, out_size - used, "%s%s", used > 0 ? "," : "", rel);
        if (written < 0 || (size_t)written >= out_size - used) {
            return false;
        }
        used += (size_t)written;
    }

    return true;
}

/* Public slot API ---------------------------------------------------------- */

bool app_disk_slot_set(app_disk_slot *slot, const char *path)
{
    disk_slot_free(slot);
    if (path == NULL || path[0] == '\0') {
        return true;
    }
    return disk_slot_append(slot, path);
}

bool app_disk_slot_append(app_disk_slot *slot, const char *path)
{
    if (slot == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    return disk_slot_append(slot, path);
}

void app_disk_slot_clear(app_disk_slot *slot)
{
    disk_slot_free(slot);
}

app_disk_slot *app_options_diskii_queue(app_options *options, int slot, int drive)
{
    if (options == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return NULL;
    }
    return &options->disk_slots[slot * 2 + drive];
}

const app_disk_slot *app_options_diskii_queue_const(
    const app_options *options, int slot, int drive)
{
    if (options == NULL || slot < 1 || slot > 7 || drive < 0 || drive > 1) {
        return NULL;
    }
    return &options->disk_slots[slot * 2 + drive];
}

void app_options_rebuild_diskii_queues(app_options *options)
{
    int i;

    if (options == NULL) {
        return;
    }
    for (i = 0; i < A2M_DISK_SLOT_COUNT; ++i) {
        app_disk_slot_clear(&options->disk_slots[i]);
    }
    for (i = 0; i < options->diskii_count; i++) {
        int slot_number = options->diskii[i].slot;
        int drive = options->diskii[i].drive;
        const char *path = options->diskii[i].path;
        app_disk_slot *queue = app_options_diskii_queue(options, slot_number, drive);

        if (queue == NULL || path == NULL || path[0] == '\0') {
            continue;
        }
        (void)app_disk_slot_append(queue, path);
    }
    for (i = 0; i < A2M_DISK_SLOT_COUNT; ++i) {
        options->disk_slots[i].current = 0;
    }
}

bool app_disk_slot_copy(app_disk_slot *dest, const app_disk_slot *src)
{
    int i;

    disk_slot_free(dest);
    for (i = 0; i < src->count; ++i) {
        if (!disk_slot_append(dest, src->paths[i])) {
            disk_slot_free(dest);
            return false;
        }
        dest->writable[i] = src->writable != NULL && src->writable[i];
    }
    dest->current = src->current;
    dest->power_on_only = src->power_on_only;
    return true;
}

const char *app_disk_slot_eject_current(app_disk_slot *slot)
{
    int i;

    if (slot == NULL || slot->count == 0) {
        return NULL;
    }

    free(slot->paths[slot->current]);
    for (i = slot->current; i < slot->count - 1; ++i) {
        slot->paths[i] = slot->paths[i + 1];
        slot->writable[i] = slot->writable[i + 1];
    }
    slot->paths[slot->count - 1] = NULL;
    slot->count--;

    if (slot->count == 0) {
        free(slot->paths);
        free(slot->writable);
        slot->paths = NULL;
        slot->writable = NULL;
        slot->current = 0;
        return NULL;
    }

    if (slot->current >= slot->count) {
        slot->current = 0;
    }
    return slot->paths[slot->current];
}

bool app_disk_slot_add_after_current(app_disk_slot *slot, const char *path)
{
    char **grown;
    bool *grown_writable;
    char *copy;
    int insert_at;
    int i;

    if (slot == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    copy = copy_string(path);
    if (copy == NULL) {
        return false;
    }

    grown = (char **)realloc(slot->paths, (size_t)(slot->count + 1) * sizeof(char *));
    if (grown == NULL) {
        free(copy);
        return false;
    }
    slot->paths = grown;

    grown_writable = (bool *)realloc(slot->writable, (size_t)(slot->count + 1) * sizeof(bool));
    if (grown_writable == NULL) {
        free(copy);
        return false;
    }
    slot->writable = grown_writable;

    insert_at = slot->count == 0 ? 0 : slot->current + 1;
    for (i = slot->count; i > insert_at; --i) {
        slot->paths[i] = slot->paths[i - 1];
        slot->writable[i] = slot->writable[i - 1];
    }
    slot->paths[insert_at] = copy;
    slot->writable[insert_at] = false;
    slot->count++;
    return true;
}

const char *app_disk_slot_select(app_disk_slot *slot, int index)
{
    if (slot == NULL || index < 0 || index >= slot->count) {
        return NULL;
    }
    slot->current = index;
    return slot->paths[index];
}

bool app_disk_slot_current_writable(const app_disk_slot *slot)
{
    if (slot == NULL || slot->count == 0 || slot->current < 0 || slot->current >= slot->count ||
        slot->writable == NULL) {
        return false;
    }
    return slot->writable[slot->current];
}

bool app_disk_slot_set_current_writable(app_disk_slot *slot, bool writable)
{
    if (slot == NULL || slot->count == 0 || slot->current < 0 || slot->current >= slot->count ||
        slot->writable == NULL) {
        return false;
    }
    slot->writable[slot->current] = writable;
    return true;
}

/* --- disk / SmartPort CLI mount collection (Apple sNdN form) ------------- */

static bool collect_mount_args(app_options *options, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *val = NULL;
        int is_disk = 0;
        int is_hd = 0;

        if (arg == NULL) {
            break;
        }
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--disk") == 0) {
            is_disk = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "a2m: %s requires a value\n", arg);
                return false;
            }
            val = argv[++i];
        } else if (strncmp(arg, "--disk=", 7) == 0) {
            is_disk = 1;
            val = arg + 7;
        } else if (strcmp(arg, "--hd") == 0 || strcmp(arg, "--smart") == 0) {
            is_hd = 1;
            if (i + 1 >= argc) {
                fprintf(stderr, "a2m: %s requires a value\n", arg);
                return false;
            }
            val = argv[++i];
        } else if (strncmp(arg, "--hd=", 5) == 0) {
            is_hd = 1;
            val = arg + 5;
        } else if (strncmp(arg, "--smart=", 8) == 0) {
            is_hd = 1;
            val = arg + 8;
        }

        if (is_disk) {
            if (!add_diskii_spec(options, val)) {
                return false;
            }
        } else if (is_hd) {
            if (!add_smartport_spec(options, val)) {
                return false;
            }
        }
    }
    return true;
}

/* Keys in the [browse] section, indexed by frontend_browse_slot / APP_BROWSE_DIR
   order. Keep in sync with frontend_browse_slot in frontend/frontend.h. */
static const char *const browse_dir_keys[APP_BROWSE_DIR_COUNT] = {
    "assembler", "floppy", "smartport", "binary", "basic", "snapshot"
};
/* Index of the "snapshot" slot within browse_dir_keys / browse_dirs. Doubles as
   the quicksave folder (see the frontend Paths tab). */
#define APP_BROWSE_DIR_SNAPSHOT 5

/* Write Apple media mounts (DiskII / SmartPort) into cfg. Shared by full-shutdown
   save and "Save Paths Only". */
static void config_write_rom_config(config *cfg, const app_options *options)
{
    char key[16];
    int i;
    int device;

    /* Drop C64 ROM product keys if present. */
    config_remove_prefix(cfg, "roms", "");
    config_remove_prefix(cfg, "rom", "");

    config_remove_prefix(cfg, "DiskII", "");
    config_remove_prefix(cfg, "SmartPort", "");
    for (i = 1; i <= 7; ++i) {
        for (device = 0; device < 2; ++device) {
            char list[8192];

            if (!format_diskii_mount_list(options, i, device, list, sizeof(list)) ||
                list[0] == '\0') {
                continue;
            }
            snprintf(key, sizeof(key), "s%dd%d", i, device);
            config_set(cfg, "DiskII", key, list);
        }
    }
    for (i = 0; i < options->smartport_count; i++) {
        char rel[PATH_MAX];
        const char *path;
        if (options->smartport[i].path == NULL || options->smartport[i].path[0] == '\0') {
            continue;
        }
        path = options->smartport[i].path;
        if (app_options_path_relative_to_ini(options, path, rel, sizeof(rel))) {
            path = rel;
        }
        snprintf(
            key,
            sizeof(key),
            "s%dd%d",
            options->smartport[i].slot,
            options->smartport[i].unit);
        config_set(cfg, "SmartPort", key, path);
    }
    if (options->smartport_boot_slot >= 1 && options->smartport_boot_slot <= 7) {
        config_set_int(cfg, "SmartPort", "boot_slot", options->smartport_boot_slot);
    }

    /* An empty value represents an installed controller with no media. */
    for (i = 1; i <= 7; ++i) {
        if (options->slot_cards[i] != APP_SLOT_CARD_DISKII &&
            options->slot_cards[i] != APP_SLOT_CARD_SMARTPORT) {
            continue;
        }
        for (device = 0; device < 2; ++device) {
            const char *section = options->slot_cards[i] == APP_SLOT_CARD_DISKII
                ? "DiskII" : "SmartPort";
            snprintf(key, sizeof(key), "s%dd%d", i, device);
            if (config_get(cfg, section, key) == NULL) {
                config_set(cfg, section, key, "");
            }
        }
    }
}

static void apply_config(app_options *options, config *cfg)
{
    const char *value;
    int i;
    int n;

    if (cfg == NULL) {
        return;
    }

    options->remember = config_get_bool(cfg, "config", "Save", options->remember);
    if (!options->remember) {
        options->remember = config_get_bool(cfg, "config", "remember", options->remember);
    }
    options->scroll_wheel_lines = config_get_int(
        cfg, "config", "scroll_wheel_lines", options->scroll_wheel_lines);
    if (options->scroll_wheel_lines < 1) {
        options->scroll_wheel_lines = 1;
    }
    options->original_del = config_get_bool(
        cfg, "config", "original_del", options->original_del);
    value = config_get(cfg, "config", "symbol_files");
    if (value == NULL) {
        value = config_get(cfg, "config", "symbols");
    }
    if (value != NULL) {
        replace_string(&options->symbol_files, value);
    }
    value = config_get(cfg, "Video", "standard");
    if (value != NULL) {
        replace_string(&options->video_standard, value);
    }
    options->true_aspect = config_get_bool(
        cfg, "Video", "true_aspect", options->true_aspect);
    options->crt_smoothing = config_get_bool(
        cfg, "Video", "crt_smoothing", options->crt_smoothing);
    options->crt_scanlines = config_get_bool(
        cfg, "Video", "crt_scanlines", options->crt_scanlines);
    options->crt_scanline_strength = config_get_int(
        cfg, "Video", "crt_scanline_strength", options->crt_scanline_strength);
    /* Floor of 1: crt_scanlines is what turns the effect off, so 0 would only
       describe an enabled effect that draws nothing. */
    if (options->crt_scanline_strength < 1) {
        options->crt_scanline_strength = 1;
    }
    if (options->crt_scanline_strength > 100) {
        options->crt_scanline_strength = 100;
    }
    options->crt_curvature = config_get_bool(
        cfg, "Video", "crt_curvature", options->crt_curvature);
    options->crt_curvature_amount = config_get_int(
        cfg, "Video", "crt_curvature_amount", options->crt_curvature_amount);
    if (options->crt_curvature_amount < 1) {
        options->crt_curvature_amount = 1;
    }
    if (options->crt_curvature_amount > 100) {
        options->crt_curvature_amount = 100;
    }
    value = config_get(cfg, "input", "keyboard_joystick_layout");
    if (value != NULL) {
        replace_string(&options->keyboard_joystick_layout, value);
    }
    options->keyboard_joystick_port = config_get_int(
        cfg, "input", "keyboard_joystick_port", options->keyboard_joystick_port);
    if (options->keyboard_joystick_port < 0 || options->keyboard_joystick_port > 2) {
        options->keyboard_joystick_port = 0;
    }
    options->keyboard_joystick_swap_buttons = config_get_bool(
        cfg,
        "input",
        "keyboard_joystick_swap_buttons",
        options->keyboard_joystick_swap_buttons);

    options->window_width = config_get_int(
        cfg, "Window", "width", options->window_width);
    if (options->window_width == 0) {
        options->window_width = config_get_int(cfg, "window", "width", 0);
    }
    options->window_height = config_get_int(
        cfg, "Window", "height", options->window_height);
    if (options->window_height == 0) {
        options->window_height = config_get_int(cfg, "window", "height", 0);
    }
    options->layout_split_display_right = config_get_float(
        cfg, "Layout", "split_display_right", options->layout_split_display_right);
    options->layout_split_top_bottom = config_get_float(
        cfg, "Layout", "split_top_bottom", options->layout_split_top_bottom);
    options->layout_split_memory_misc = config_get_float(
        cfg, "Layout", "split_memory_misc", options->layout_split_memory_misc);
    value = config_get(cfg, "config", "turbo_speeds");
    if (value != NULL) {
        replace_string(&options->turbo_multipliers, value);
    }

    /* Apple machine model. Slot cards, including Mockingboard, come from [Slots]. */
    value = config_get(cfg, "machine", "model");
    if (value == NULL) {
        value = config_get(cfg, "Machine", "Model");
    }
    if (value != NULL && value[0] != '\0') {
        int model = 0;
        if (app_model_from_string(value, &model)) {
            options->apple_model = model;
        }
    }
    options->show_disk_leds = config_get_bool(
        cfg, "disk", "show_disk_leds",
        config_get_bool(cfg, "config", "disk_leds", options->show_disk_leds));
    /* Absent key defaults to false. */
    options->pause_on_brk = config_get_bool(cfg, "config", "pause_on_brk", options->pause_on_brk);
    options->history_off_on_max = config_get_bool(
        cfg, "config", "history_off_on_max", options->history_off_on_max);

    /* Legacy single-path keys (also accept a comma-separated queue). */
    value = config_get(cfg, "disk", "path");
    if (value != NULL && value[0] != '\0') {
        (void)add_diskii_mounts_from_list(options, 6, 0, value);
    }
    value = config_get(cfg, "disk", "hd");
    if (value != NULL && value[0] != '\0') {
        char abs_path[PATH_MAX];
        if (path_absolute_from_ini(options, value, abs_path, sizeof(abs_path))) {
            (void)add_smartport_mount(options, 7, 0, abs_path);
        } else {
            (void)add_smartport_mount(options, 7, 0, value);
        }
    }

    /* a2m-style [DiskII] s6d0=path / [SmartPort] s7d0=path */
    n = config_entry_count(cfg);
    for (i = 0; i < n; i++) {
        const char *section = NULL;
        const char *key = NULL;
        const char *val = NULL;
        int slot = 0;
        int unit = 0;

        if (!config_entry_at(cfg, i, &section, &key, &val)) {
            continue;
        }
        if (!app_options_parse_slot_unit_key(key, &slot, &unit)) {
            continue;
        }
        if (section_is(section, "diskii") || section_is(section, "DiskII")) {
            (void)app_options_set_slot_card(options, slot, APP_SLOT_CARD_DISKII);
            if (val != NULL && val[0] != '\0' && val[0] != ';') {
                (void)add_diskii_mounts_from_list(options, slot, unit, val);
            }
        } else if (section_is(section, "smartport") || section_is(section, "SmartPort")) {
            (void)app_options_set_slot_card(options, slot, APP_SLOT_CARD_SMARTPORT);
            if (val != NULL && val[0] != '\0' && val[0] != ';') {
                char abs_path[PATH_MAX];
                if (path_absolute_from_ini(options, val, abs_path, sizeof(abs_path))) {
                    (void)add_smartport_mount(options, slot, unit, abs_path);
                } else {
                    (void)add_smartport_mount(options, slot, unit, val);
                }
            }
        }
    }

    /* Media entries imply a controller for legacy INIs. */
    for (i = 0; i < options->diskii_count; ++i) {
        (void)app_options_set_slot_card(
            options, options->diskii[i].slot, APP_SLOT_CARD_DISKII);
    }
    for (i = 0; i < options->smartport_count; ++i) {
        (void)app_options_set_slot_card(
            options, options->smartport[i].slot, APP_SLOT_CARD_SMARTPORT);
    }

    /* Explicit slot keys are authoritative, including Empty. */
    for (i = 1; i <= 7; ++i) {
        char key[16];
        app_slot_card_type type;
        snprintf(key, sizeof(key), "slot%d", i);
        value = config_get(cfg, "Slots", key);
        if (value == NULL) {
            value = config_get(cfg, "slots", key);
        }
        if (value != NULL && app_slot_card_from_string(value, &type)) {
            (void)app_options_set_slot_card(options, i, type);
        }
    }
    options->smartport_boot_slot = config_get_int(cfg, "SmartPort", "boot_slot", 0);
    if (options->smartport_boot_slot < 0 || options->smartport_boot_slot > 7) {
        options->smartport_boot_slot = 0;
    }
    app_options_reconcile_slot_cards(options);
    value = config_get(cfg, "debug", "history_memory_mb");
    if (value != NULL) {
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 0);
        if (end == value || *end != '\0' ||
            (parsed != 0u && (parsed < 16u || parsed > 4096u))) {
            fprintf(
                stderr,
                "invalid [debug] history_memory_mb `%s`; using %d\n",
                value,
                A2M_DEFAULT_HISTORY_MEMORY_MB);
            options->history_memory_mb = A2M_DEFAULT_HISTORY_MEMORY_MB;
        } else {
            options->history_memory_mb = (int)parsed;
        }
    }
    value = config_get(cfg, "debug", "frame_ring_memory_mb");
    if (value != NULL) {
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 0);
        if (end == value || *end != '\0' ||
            (parsed != 0u && (parsed < 8u || parsed > 4096u))) {
            fprintf(
                stderr,
                "invalid [debug] frame_ring_memory_mb `%s`; using %d\n",
                value,
                A2M_DEFAULT_FRAME_RING_MEMORY_MB);
            options->frame_ring_memory_mb = A2M_DEFAULT_FRAME_RING_MEMORY_MB;
        } else {
            options->frame_ring_memory_mb = (int)parsed;
        }
    }
    options->timemachine = config_get_bool(
        cfg, "debug", "timemachine", options->timemachine);
    value = config_get(cfg, "debug", "timemachine_memory_mb");
    if (value != NULL) {
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 0);
        if (end == value || *end != '\0' ||
            (parsed != 0u && (parsed < 16u || parsed > 4096u))) {
            fprintf(
                stderr,
                "invalid [debug] timemachine_memory_mb `%s`; using %d\n",
                value,
                A2M_DEFAULT_TIMEMACHINE_MEMORY_MB);
            options->timemachine_memory_mb = A2M_DEFAULT_TIMEMACHINE_MEMORY_MB;
        } else {
            options->timemachine_memory_mb = (int)parsed;
        }
    }

    value = config_get(cfg, "assembler", "file");
    if (value != NULL) {
        char abs_path[PATH_MAX];
        if (path_absolute_from_ini(options, value, abs_path, sizeof(abs_path))) {
            replace_string(&options->assembler_file, abs_path);
        } else {
            replace_string(&options->assembler_file, value);
        }
    }
    value = config_get(cfg, "assembler", "address");
    if (value != NULL) {
        replace_string(&options->assembler_address, value);
    }
    value = config_get(cfg, "assembler", "run_address");
    if (value != NULL) {
        replace_string(&options->assembler_run_address, value);
    }
    options->assembler_use_address = config_get_bool(
        cfg, "assembler", "use_address", options->assembler_use_address);
    options->assembler_auto_run = config_get_bool(
        cfg, "assembler", "auto_run", options->assembler_auto_run);
    options->assembler_mli_launch = config_get_bool(
        cfg, "assembler", "mli_launch", options->assembler_mli_launch);
    options->assembler_reset_first = config_get_bool(
        cfg, "assembler", "reset", options->assembler_reset_first);
    if (!options->assembler_auto_run) {
        options->assembler_mli_launch = false;
    }
    if (options->assembler_mli_launch) {
        options->assembler_reset_first = false;
    }
    options->assembler_rearm_oneshots = config_get_bool(
        cfg, "assembler", "rearm_oneshots", options->assembler_rearm_oneshots);
    options->assembler_auto_adjust_segments = config_get_bool(
        cfg, "assembler", "auto_adjust_segments",
        options->assembler_auto_adjust_segments);

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        value = config_get(cfg, "browse", browse_dir_keys[i]);
        if (value != NULL && value[0] != '\0') {
            replace_string(&options->browse_dirs[i], value);
        }
    }
    /* Migrate the pre-unification [state] quicksave_folder into the snapshot slot
       when no [browse] snapshot is present. */
    if (options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT] == NULL ||
            options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT][0] == '\0') {
        value = config_get(cfg, "state", "quicksave_folder");
        if (value != NULL && value[0] != '\0') {
            replace_string(&options->browse_dirs[APP_BROWSE_DIR_SNAPSHOT], value);
        }
    }
}

static bool apply_video_standard_arg(app_options *options, const char *value)
{
    if (value == NULL) {
        return true;
    }
    if (strcmp(value, "PAL") == 0 || strcmp(value, "NTSC") == 0) {
        return replace_string(&options->video_standard, value);
    }

    fprintf(stderr, "invalid video standard `%s`; expected PAL or NTSC\n", value);
    return false;
}

static bool preparse_ini_options(app_options *options, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--defaults") == 0 || strcmp(arg, "-f") == 0) {
            options->defaults = true;
            options->use_ini = false;
        } else if (strcmp(arg, "--noini") == 0 || strcmp(arg, "-n") == 0) {
            options->use_ini = false;
        } else if (strcmp(arg, "--inifile") == 0 || strcmp(arg, "-i") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a value\n", arg);
                return false;
            }
            if (!replace_string(&options->ini_path, argv[++i])) {
                return false;
            }
        } else if (strncmp(arg, "--inifile=", 10) == 0) {
            if (!replace_string(&options->ini_path, arg + 10)) {
                return false;
            }
        } else if (strncmp(arg, "-i", 2) == 0 && arg[2] != '\0') {
            if (!replace_string(&options->ini_path, arg + 2)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_command_line_overrides(app_options *options, int argc, char **argv)
{
    int defaults = 0;
    int noini = 0;
    int no_save_ini = 0;
    int remember = 0;
    int save_ini = 0;
    int audio_smoke = 0;
    int control_port = 0;
    int headless = 0;
    int mb_slot = -1;
    int kbdjoy_port = -1;
    const char *breakpoint = NULL;
    const char *ini_path = NULL;
    const char *sna_path = NULL;
    const char *audio_record_path = NULL;
    const char *turbo = NULL;
    const char *model_s = NULL;
    const char *symbols_s = NULL;
    const char *history_memory = NULL;
    const char *timemachine_memory = NULL;
    int history_off_on_max_flag = 0; /* argparse counter; presence via argv scan */
    int history_off_on_max_cli = 0;
    int history_off_on_max_seen = 0;
    int timemachine_flag = 0;
    int timemachine_cli = 0;
    int timemachine_seen = 0;
    const char *disk_help = NULL;
    const char *hd_help = NULL;
    const char *kbdjoy_layout = NULL;
    float audio_record_start = 0.0f;
    float audio_record_duration = 0.0f;
    int show_version = 0;
    struct argparse argparse;
    const char *const usages[] = {
        "a2m [options]",
        NULL,
    };
    struct argparse_option parse_options[] = {
        OPT_HELP(),
        OPT_BOOLEAN('V', "version", &show_version, "print version and exit", NULL, 0, 0),
        OPT_BOOLEAN('A', "audio-smoke", &audio_smoke, "emit 440 Hz tone to verify audio path", NULL, 0, OPT_NONEG),
        OPT_STRING('\0', "audio-record", &audio_record_path, "record runtime mono audio to WAV", NULL, 0, 0),
        OPT_FLOAT('\0', "audio-record-start", &audio_record_start, "recording start time in seconds", NULL, 0, 0),
        OPT_FLOAT('\0', "audio-record-duration", &audio_record_duration, "recording duration in seconds", NULL, 0, 0),
        OPT_STRING('b', "break", &breakpoint, "install execute breakpoint at hex address", NULL, 0, 0),
        OPT_INTEGER('\0', "control-port", &control_port,
                    "listen on localhost TCP for A2M/12 remote control (0=off)", NULL, 0, 0),
        OPT_BOOLEAN('\0', "headless", &headless,
                    "no window; short smoke exit unless --control-port is set (long-lived)",
                    NULL, 0, OPT_NONEG),
        OPT_STRING('\0', "history-memory", &history_memory, "CPU flight-recorder memory budget in MiB (0 or 16..4096)", NULL, 0, 0),
        OPT_BOOLEAN('\0', "history-off-on-max", &history_off_on_max_flag,
                    "pause CPU history while turbo is max (default on; --no-history-off-on-max)",
                    NULL, 0, 0),
        OPT_BOOLEAN('\0', "timemachine", &timemachine_flag,
                    "enable TimeMachine recording (default off; --no-timemachine)",
                    NULL, 0, 0),
        OPT_STRING('\0', "timemachine-memory", &timemachine_memory,
                   "TimeMachine checkpoint-ring budget in MiB (0 or 16..4096; default 128)",
                   NULL, 0, 0),
        OPT_BOOLEAN('f', "defaults", &defaults, "use default settings", NULL, 0, OPT_NONEG),
        OPT_STRING('d', "disk", &disk_help,
                   "Disk II mount: path or s6d0=path (repeatable; same drive appends queue)",
                   NULL, 0, 0),
        OPT_STRING('\0', "hd", &hd_help, "SmartPort mount: path or s7d0=path (repeatable; default s7d0)", NULL, 0, 0),
        OPT_STRING('\0', "smart", &hd_help, "alias for --hd", NULL, 0, 0),
        OPT_STRING('i', "inifile", &ini_path, "path to an .ini file (default a2m.ini)", NULL, 0, 0),
        OPT_BOOLEAN('n', "noini", &noini, "do not use an ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('!', "nosaveini", &no_save_ini, "do not save the ini no matter what", NULL, 0, OPT_NONEG),
        OPT_STRING('\0', "sna", &sna_path, "load machine snapshot at startup", NULL, 0, 0),
        OPT_STRING('m', "model", &model_s, "machine model: enh (//e Enhanced) or plus (][+)", NULL, 0, 0),
        OPT_INTEGER('\0', "mb-slot", &mb_slot, "Mockingboard slot 1..7 (default 4; 0=disable)", NULL, 0, 0),
        OPT_STRING('\0', "symbols", &symbols_s, "load simple symbol file (NAME hex per line)", NULL, 0, 0),
        OPT_BOOLEAN('r', "remember", &remember, "add save at quit to ini file", NULL, 0, OPT_NONEG),
        OPT_BOOLEAN('v', "saveini", &save_ini, "save to ini file at quit", NULL, 0, OPT_NONEG),
        OPT_STRING('t', "turbo", &turbo, "turbo ladder CSV: MHz and/or max (e.g. 1,max or 1,4,8,max)", NULL, 0, 0),
        OPT_INTEGER('\0', "kbdjoy", &kbdjoy_port,
                    "keyboard joystick on gameport stick: 0 off, 1 or 2", NULL, 0, 0),
        OPT_STRING('\0', "kbdjoy-layout", &kbdjoy_layout,
                   "keyboard joystick layout: numpad or wasd", NULL, 0, 0),
        OPT_END(),
    };

    argparse_init(&argparse, parse_options, usages, 0);
    argparse_describe(
        &argparse,
        "\na2m — Apple ][+ / //e Enhanced emulator with integrated debugger.",
        "\nDisk/HD: repeat -d/--disk and --hd/--smart; form s6d0=path.\n"
        "  Same drive twice (e.g. -d s6d0=a.nib -d s6d0=b.nib) builds a multi-image queue.\n"
        "Keyboard stick: --kbdjoy 0|1|2 and --kbdjoy-layout numpad|wasd.\n"
        "INI: --noini / --saveini / --remember / --defaults.\n");
    argparse_parse(&argparse, argc, (const char **)argv);
    (void)disk_help;
    (void)hd_help;

    if (show_version) {
        options->show_version = true;
        return true;
    }

    if (defaults) {
        options->defaults = true;
        options->use_ini = false;
    }
    if (ini_path != NULL) {
        replace_string(&options->ini_path, ini_path);
    }
    if (noini) {
        options->use_ini = false;
    }
    if (breakpoint != NULL) {
        replace_string(&options->breakpoint, breakpoint);
    }
    if (!collect_mount_args(options, argc, argv)) {
        return false;
    }
    {
        int mount_index;
        for (mount_index = 0; mount_index < options->diskii_count; ++mount_index) {
            (void)app_options_set_slot_card(
                options, options->diskii[mount_index].slot, APP_SLOT_CARD_DISKII);
        }
        for (mount_index = 0; mount_index < options->smartport_count; ++mount_index) {
            (void)app_options_set_slot_card(
                options, options->smartport[mount_index].slot, APP_SLOT_CARD_SMARTPORT);
        }
    }
    app_options_sync_convenience_paths(options);
    app_options_rebuild_diskii_queues(options);
    if (sna_path != NULL) {
        replace_string(&options->sna_path, sna_path);
    }
    if (turbo != NULL) {
        replace_string(&options->turbo_multipliers, turbo);
    }
    if (model_s != NULL && model_s[0] != '\0') {
        int model = 0;
        if (!app_model_from_string(model_s, &model)) {
            fprintf(stderr, "a2m: --model must be enh or plus\n");
            return false;
        }
        options->apple_model = model;
    }
    if (mb_slot >= 0) {
        if (mb_slot > 7) {
            fprintf(stderr, "a2m: --mb-slot must be 0..7\n");
            return false;
        }
        if (options->mb_slot >= 1 && options->mb_slot <= 7 &&
            options->slot_cards[options->mb_slot] == APP_SLOT_CARD_MOCKINGBOARD) {
            (void)app_options_set_slot_card(
                options, options->mb_slot, APP_SLOT_CARD_EMPTY);
        }
        if (mb_slot >= 1) {
            (void)app_options_set_slot_card(options, mb_slot, APP_SLOT_CARD_MOCKINGBOARD);
        }
    }
    if (symbols_s != NULL && symbols_s[0] != '\0') {
        replace_string(&options->symbol_files, symbols_s);
    }
    if (kbdjoy_port >= 0) {
        if (kbdjoy_port > 2) {
            fprintf(stderr, "a2m: --kbdjoy expects 0, 1, or 2\n");
            return false;
        }
        options->keyboard_joystick_port = kbdjoy_port;
    }
    if (kbdjoy_layout != NULL) {
        if (strcmp(kbdjoy_layout, "numpad") != 0 && strcmp(kbdjoy_layout, "wasd") != 0) {
            fprintf(stderr, "a2m: --kbdjoy-layout expects 'numpad' or 'wasd'\n");
            return false;
        }
        replace_string(&options->keyboard_joystick_layout, kbdjoy_layout);
    }

    if (remember) {
        options->remember = true;
        options->save_ini = true;
    }
    if (save_ini) {
        options->save_ini = true;
    }
    if (no_save_ini) {
        options->no_save_ini = true;
        options->save_ini = false;
    }
    if (audio_smoke) {
        options->audio_smoke = true;
    }
    if (audio_record_path != NULL) {
        replace_string(&options->audio_record_path, audio_record_path);
    }
    if (audio_record_start > 0.0f) {
        options->audio_record_start_seconds = audio_record_start;
    }
    if (audio_record_duration > 0.0f) {
        options->audio_record_duration_seconds = audio_record_duration;
    }
    if (control_port < 0 || control_port > 65535) {
        fprintf(stderr, "invalid control port `%d`; expected 0..65535\n", control_port);
        return false;
    }
    if (control_port > 0) {
        options->control_port = control_port;
    }
    if (headless) {
        options->headless = true;
    }
    if (history_memory != NULL) {
        char *end = NULL;
        unsigned long parsed = strtoul(history_memory, &end, 0);
        if (end == history_memory || *end != '\0' ||
            (parsed != 0u && (parsed < 16u || parsed > 4096u))) {
            fprintf(
                stderr,
                "--history-memory expects 0 or a value from 16 through 4096 MiB\n");
            return false;
        }
        options->history_memory_mb = (int)parsed;
    }
    /* argparse BOOLEAN increments; detect presence so INI is not clobbered. */
    {
        int ai;
        for (ai = 1; ai < argc; ai++) {
            if (argv[ai] == NULL) {
                continue;
            }
            if (strcmp(argv[ai], "--history-off-on-max") == 0) {
                history_off_on_max_seen = 1;
                history_off_on_max_cli = 1;
            } else if (strcmp(argv[ai], "--no-history-off-on-max") == 0) {
                history_off_on_max_seen = 1;
                history_off_on_max_cli = 0;
            } else if (strcmp(argv[ai], "--timemachine") == 0) {
                timemachine_seen = 1;
                timemachine_cli = 1;
            } else if (strcmp(argv[ai], "--no-timemachine") == 0) {
                timemachine_seen = 1;
                timemachine_cli = 0;
            }
        }
        if (history_off_on_max_seen) {
            options->history_off_on_max = history_off_on_max_cli != 0;
        }
        if (timemachine_seen) {
            options->timemachine = timemachine_cli != 0;
        }
    }
    (void)history_off_on_max_flag;
    (void)timemachine_flag;
    if (timemachine_memory != NULL) {
        char *end = NULL;
        unsigned long parsed = strtoul(timemachine_memory, &end, 0);
        if (end == timemachine_memory || *end != '\0' ||
            (parsed != 0u && (parsed < 16u || parsed > 4096u))) {
            fprintf(
                stderr,
                "--timemachine-memory expects 0 or a value from 16 through 4096 MiB\n");
            return false;
        }
        options->timemachine_memory_mb = (int)parsed;
    }

    return true;
}

void app_options_init(app_options *options)
{
    memset(options, 0, sizeof(*options));
    options->use_ini = true;
    replace_string(&options->ini_path, A2M_DEFAULT_INI);
    options->scroll_wheel_lines = A2M_DEFAULT_SCROLL_WHEEL_LINES;
    options->original_del = false;
    replace_string(&options->video_standard, A2M_DEFAULT_VIDEO_STANDARD);
    options->crt_scanline_strength = A2M_DEFAULT_CRT_SCANLINE_STRENGTH;
    options->crt_curvature_amount = A2M_DEFAULT_CRT_CURVATURE_AMOUNT;
    replace_string(&options->keyboard_joystick_layout,
                   A2M_DEFAULT_KEYBOARD_JOYSTICK_LAYOUT);
    /* Default stick 1 so Apple titles (e.g. Total Replay menus) get a
       keyboard stick without a pad. Set 0 in INI to disable. */
    options->keyboard_joystick_port = 1;
    options->keyboard_joystick_swap_buttons = false;
    options->window_width = 0;
    options->window_height = 0;
    options->layout_split_display_right = A2M_DEFAULT_LAYOUT_SPLIT_DISPLAY_RIGHT;
    options->layout_split_top_bottom = A2M_DEFAULT_LAYOUT_SPLIT_TOP_BOTTOM;
    options->layout_split_memory_misc = A2M_DEFAULT_LAYOUT_SPLIT_MEMORY_MISC;
    options->assembler_use_address = true;
    options->assembler_auto_run = false;
    options->assembler_mli_launch = false;
    options->assembler_reset_first = true;
    options->assembler_rearm_oneshots = false;
    options->assembler_auto_adjust_segments = false;
    options->control_port = 0;
    options->headless = false;
    options->show_disk_leds = true;
    options->history_memory_mb = A2M_DEFAULT_HISTORY_MEMORY_MB;
    options->history_off_on_max = true; /* max free-run boost by default */
    options->frame_ring_memory_mb = A2M_DEFAULT_FRAME_RING_MEMORY_MB;
    options->timemachine = false;
    options->timemachine_memory_mb = A2M_DEFAULT_TIMEMACHINE_MEMORY_MB;
    options->apple_model = 0; /* //e Enhanced */
    options->mb_slot = 4;
    options->slot_cards[4] = APP_SLOT_CARD_MOCKINGBOARD;
    options->slot_cards[6] = APP_SLOT_CARD_DISKII;
    options->slot_cards[7] = APP_SLOT_CARD_SMARTPORT;
    options->diskii_count = 0;
    options->smartport_count = 0;
    options->smartport_boot_slot = 0;
    replace_string(&options->turbo_multipliers, "1,max");
    replace_string(&options->disk_s6d0, "");
    replace_string(&options->disk_s6d1, "");
    replace_string(&options->hd_s7d0, "");
    replace_string(&options->hd_s5d0, "");
}

bool app_options_apply_ini_file(app_options *options, const char *path)
{
    config *cfg;

    if (options == NULL || path == NULL || path[0] == '\0') {
        return false;
    }

    cfg = config_load(path);
    if (cfg == NULL) {
        return false;
    }

    /* Media paths in this file are relative to it, not to a previous ini_path. */
    if (!replace_string(&options->ini_path, path)) {
        config_destroy(cfg);
        return false;
    }

    apply_config(options, cfg);
    config_destroy(cfg);
    return true;
}

bool app_options_copy(app_options *dest, const app_options *src)
{
    int i;

    if (dest == NULL || src == NULL) {
        return false;
    }

    app_options_init(dest);
    dest->use_ini = src->use_ini;
    dest->save_ini = src->save_ini;
    dest->remember = src->remember;
    dest->defaults = src->defaults;
    dest->no_save_ini = src->no_save_ini;
    dest->autorun = src->autorun;
    dest->show_disk_leds = src->show_disk_leds;
    dest->pause_on_brk = src->pause_on_brk;
    dest->history_off_on_max = src->history_off_on_max;
    dest->true_aspect = src->true_aspect;
    dest->crt_smoothing = src->crt_smoothing;
    dest->crt_scanlines = src->crt_scanlines;
    dest->crt_scanline_strength = src->crt_scanline_strength;
    dest->crt_curvature = src->crt_curvature;
    dest->crt_curvature_amount = src->crt_curvature_amount;
    dest->audio_smoke = src->audio_smoke;
    dest->audio_record_start_seconds = src->audio_record_start_seconds;
    dest->audio_record_duration_seconds = src->audio_record_duration_seconds;
    dest->scroll_wheel_lines = src->scroll_wheel_lines;
    dest->original_del = src->original_del;
    dest->window_width = src->window_width;
    dest->window_height = src->window_height;
    dest->layout_split_display_right = src->layout_split_display_right;
    dest->layout_split_top_bottom = src->layout_split_top_bottom;
    dest->layout_split_memory_misc = src->layout_split_memory_misc;

    dest->assembler_use_address = src->assembler_use_address;
    dest->assembler_auto_run = src->assembler_auto_run;
    dest->assembler_mli_launch = src->assembler_mli_launch;
    dest->assembler_reset_first = src->assembler_reset_first;
    dest->assembler_rearm_oneshots = src->assembler_rearm_oneshots;
    dest->assembler_auto_adjust_segments =
        src->assembler_auto_adjust_segments;
    dest->show_version = src->show_version;
    dest->control_port = src->control_port;
    dest->headless = src->headless;
    dest->keyboard_joystick_port = src->keyboard_joystick_port;
    dest->keyboard_joystick_swap_buttons = src->keyboard_joystick_swap_buttons;
    dest->history_memory_mb = src->history_memory_mb;
    dest->frame_ring_memory_mb = src->frame_ring_memory_mb;
    dest->timemachine = src->timemachine;
    dest->timemachine_memory_mb = src->timemachine_memory_mb;
    dest->apple_model = src->apple_model;
    dest->mb_slot = src->mb_slot;
    memcpy(dest->slot_cards, src->slot_cards, sizeof(dest->slot_cards));

    if (!replace_string(&dest->keyboard_joystick_layout, src->keyboard_joystick_layout) ||
        !replace_string(&dest->ini_path, src->ini_path) ||
        !replace_string(&dest->breakpoint, src->breakpoint) ||
        !replace_string(&dest->turbo_multipliers, src->turbo_multipliers) ||
        !replace_string(&dest->symbol_files, src->symbol_files) ||
        !replace_string(&dest->video_standard, src->video_standard) ||
        !replace_string(&dest->basic_path, src->basic_path) ||
        !replace_string(&dest->sna_path, src->sna_path) ||
        !replace_string(&dest->audio_record_path, src->audio_record_path) ||
        !replace_string(&dest->assembler_file, src->assembler_file) ||
        !replace_string(&dest->assembler_address, src->assembler_address) ||
        !replace_string(&dest->assembler_run_address, src->assembler_run_address) ||
        !replace_string(&dest->disk_s6d0, src->disk_s6d0) ||
        !replace_string(&dest->disk_s6d1, src->disk_s6d1) ||
        !replace_string(&dest->hd_s7d0, src->hd_s7d0) ||
        !replace_string(&dest->hd_s5d0, src->hd_s5d0)) {
        app_options_destroy(dest);
        return false;
    }

    for (i = 0; i < A2M_DISK_SLOT_COUNT; ++i) {
        if (!app_disk_slot_copy(&dest->disk_slots[i], &src->disk_slots[i])) {
            app_options_destroy(dest);
            return false;
        }
    }

    dest->diskii_count = 0;
    for (i = 0; i < src->diskii_count; ++i) {
        if (src->diskii[i].path == NULL) {
            continue;
        }
        if (!add_diskii_mount(dest, src->diskii[i].slot, src->diskii[i].drive, src->diskii[i].path)) {
            app_options_destroy(dest);
            return false;
        }
    }
    dest->smartport_count = 0;
    dest->smartport_boot_slot = src->smartport_boot_slot;
    for (i = 0; i < src->smartport_count; ++i) {
        if (src->smartport[i].path == NULL) {
            continue;
        }
        if (!add_smartport_mount(
                dest, src->smartport[i].slot, src->smartport[i].unit, src->smartport[i].path)) {
            app_options_destroy(dest);
            return false;
        }
    }

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        if (!replace_string(&dest->browse_dirs[i], src->browse_dirs[i])) {
            app_options_destroy(dest);
            return false;
        }
    }

    return true;
}

bool app_options_replace_media_mounts(app_options *dest, const app_options *src)
{
    int i;

    if (dest == NULL || src == NULL) {
        return false;
    }

    for (i = 0; i < dest->diskii_count; ++i) {
        free(dest->diskii[i].path);
        dest->diskii[i].path = NULL;
    }
    dest->diskii_count = 0;

    for (i = 0; i < dest->smartport_count; ++i) {
        free(dest->smartport[i].path);
        dest->smartport[i].path = NULL;
    }
    dest->smartport_count = 0;

    for (i = 0; i < src->diskii_count; ++i) {
        if (src->diskii[i].path == NULL || src->diskii[i].path[0] == '\0') {
            continue;
        }
        if (!add_diskii_mount(
                dest, src->diskii[i].slot, src->diskii[i].drive, src->diskii[i].path)) {
            return false;
        }
    }

    dest->smartport_boot_slot = src->smartport_boot_slot;
    for (i = 0; i < src->smartport_count; ++i) {
        if (src->smartport[i].path == NULL || src->smartport[i].path[0] == '\0') {
            continue;
        }
        if (!add_smartport_mount(
                dest, src->smartport[i].slot, src->smartport[i].unit, src->smartport[i].path)) {
            return false;
        }
    }

    for (i = 0; i < A2M_DISK_SLOT_COUNT; ++i) {
        if (!app_disk_slot_copy(&dest->disk_slots[i], &src->disk_slots[i])) {
            return false;
        }
    }

    app_options_sync_convenience_paths(dest);
    return true;
}

bool app_options_load_startup(app_options *options, int argc, char **argv)
{
    config *cfg = NULL;

    app_options_init(options);

    if (!preparse_ini_options(options, argc, argv)) {
        return false;
    }

    if (options->use_ini) {
        cfg = config_load(options->ini_path);
        if (cfg != NULL) {
            apply_config(options, cfg);
        }
    }

    if (!parse_command_line_overrides(options, argc, argv)) {
        config_destroy(cfg);
        return false;
    }

    config_destroy(cfg);
    return true;
}

bool app_options_save_shutdown(const app_options *options)
{
    config *cfg;
    bool ok;
    char relative_symbol_files[1024];

    if (options == NULL || options->no_save_ini || options->ini_path == NULL) {
        return true;
    }

    cfg = config_load(options->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    if (options->video_standard != NULL) {
        config_set(cfg, "Video", "standard", options->video_standard);
    }
    config_set_bool(cfg, "Video", "true_aspect", options->true_aspect);
    config_set_bool(cfg, "Video", "crt_smoothing", options->crt_smoothing);
    config_set_bool(cfg, "Video", "crt_scanlines", options->crt_scanlines);
    config_set_int(cfg, "Video", "crt_scanline_strength", options->crt_scanline_strength);
    config_set_bool(cfg, "Video", "crt_curvature", options->crt_curvature);
    config_set_int(cfg, "Video", "crt_curvature_amount", options->crt_curvature_amount);
    config_remove_prefix(cfg, "Video", "display_width");
    config_remove_prefix(cfg, "Video", "display_height");
    if (options->keyboard_joystick_layout != NULL) {
        config_set(cfg, "input", "keyboard_joystick_layout",
                   options->keyboard_joystick_layout);
    }
    config_set_int(cfg, "input", "keyboard_joystick_port",
                   options->keyboard_joystick_port);
    config_set_bool(
        cfg,
        "input",
        "keyboard_joystick_swap_buttons",
        options->keyboard_joystick_swap_buttons);

    config_remove_prefix(cfg, "runtime", "turbo");
    if (options->turbo_multipliers != NULL) {
        config_set(cfg, "config", "turbo_speeds", options->turbo_multipliers);
    }
    config_set_int(cfg, "config", "scroll_wheel_lines", options->scroll_wheel_lines);
    config_set_bool(cfg, "config", "original_del", options->original_del);
    config_set_int(cfg, "debug", "history_memory_mb", options->history_memory_mb);
    config_set_bool(cfg, "config", "history_off_on_max", options->history_off_on_max);
    config_set_int(cfg, "debug", "frame_ring_memory_mb", options->frame_ring_memory_mb);
    config_set_bool(cfg, "debug", "timemachine", options->timemachine);
    config_set_int(cfg, "debug", "timemachine_memory_mb", options->timemachine_memory_mb);
    /* Drop legacy C64 VIC-II line-ring budget if present in older INIs. */
    config_remove_prefix(cfg, "debug", "vic_ring_memory_mb");
    /* The snapshot folder is now [browse] snapshot; drop the legacy key. */
    config_remove_prefix(cfg, "state", "quicksave_folder");
    if (options->symbol_files != NULL &&
        transform_symbol_files(options, options->symbol_files, false, relative_symbol_files, sizeof(relative_symbol_files))) {
        config_set(cfg, "config", "symbol_files", relative_symbol_files);
    }
    if (options->remember) {
        config_set(cfg, "config", "Save", "yes");
    } else {
        config_remove_prefix(cfg, "config", "Save");
    }

    if (options->window_width > 0 && options->window_height > 0) {
        config_set_int(cfg, "Window", "width", options->window_width);
        config_set_int(cfg, "Window", "height", options->window_height);
    }

    config_set_float(cfg, "Layout", "split_display_right", options->layout_split_display_right);
    config_set_float(cfg, "Layout", "split_top_bottom", options->layout_split_top_bottom);
    config_set_float(cfg, "Layout", "split_memory_misc", options->layout_split_memory_misc);
    config_remove_prefix(cfg, "Layout", "display_width");
    config_remove_prefix(cfg, "Layout", "display_height");

    config_write_rom_config(cfg, options);
    /* Convenience path buffers may hold Configure-dialog edits not yet folded
       into the mount arrays; write them so paths survive save. */
    if (options->disk_s6d0 != NULL && options->disk_s6d0[0] != '\0') {
        config_set(cfg, "DiskII", "s6d0", options->disk_s6d0);
    }
    if (options->disk_s6d1 != NULL && options->disk_s6d1[0] != '\0') {
        config_set(cfg, "DiskII", "s6d1", options->disk_s6d1);
    }
    if (options->hd_s7d0 != NULL && options->hd_s7d0[0] != '\0') {
        config_set(cfg, "SmartPort", "s7d0", options->hd_s7d0);
    }
    if (options->hd_s5d0 != NULL && options->hd_s5d0[0] != '\0') {
        config_set(cfg, "SmartPort", "s5d0", options->hd_s5d0);
    }

    config_set(cfg, "machine", "model", options->apple_model == 1 ? "plus" : "enh");
    config_remove_prefix(cfg, "Slots", "");
    config_remove_prefix(cfg, "slots", "");
    config_remove_prefix(cfg, "Mockingboard", "");
    config_remove_prefix(cfg, "mockingboard", "");
    {
        int slot;
        char key[16];
        for (slot = 1; slot <= 7; ++slot) {
            snprintf(key, sizeof(key), "slot%d", slot);
            config_set(cfg, "Slots", key, app_slot_card_name(options->slot_cards[slot]));
        }
    }
    config_remove_prefix(cfg, "disk", "emulate_1541");
    config_remove_prefix(cfg, "disk", "media_1541");
    /* Default is false; only persist when set, so an absent key means false. */
    if (options->pause_on_brk) {
        config_set_bool(cfg, "config", "pause_on_brk", true);
    } else {
        config_remove_prefix(cfg, "config", "pause_on_brk");
    }
    /* Always persist so a false value survives (default is true). */
    config_set_bool(cfg, "disk", "show_disk_leds", options->show_disk_leds);
    config_set_bool(cfg, "config", "disk_leds", options->show_disk_leds);

    if (options->assembler_file != NULL && options->assembler_file[0] != '\0') {
        char rel_path[PATH_MAX];
        if (app_options_path_relative_to_ini(options, options->assembler_file, rel_path, sizeof(rel_path))) {
            config_set(cfg, "assembler", "file", rel_path);
        }
    } else {
        config_remove_prefix(cfg, "assembler", "file");
    }
    if (options->assembler_address != NULL && options->assembler_address[0] != '\0') {
        config_set(cfg, "assembler", "address", options->assembler_address);
    }
    if (options->assembler_run_address != NULL && options->assembler_run_address[0] != '\0') {
        config_set(cfg, "assembler", "run_address", options->assembler_run_address);
    }
    config_set_bool(cfg, "assembler", "use_address", options->assembler_use_address);
    config_set_bool(cfg, "assembler", "auto_run", options->assembler_auto_run);
    config_set_bool(cfg, "assembler", "mli_launch", options->assembler_mli_launch);
    /* basic_run was a C64-only paste-RUN mode and is not part of the Apple UI. */
    config_remove_prefix(cfg, "assembler", "basic_run");
    config_set_bool(cfg, "assembler", "reset", options->assembler_reset_first);
    config_set_bool(cfg, "assembler", "rearm_oneshots", options->assembler_rearm_oneshots);
    config_set_bool(
        cfg,
        "assembler",
        "auto_adjust_segments",
        options->assembler_auto_adjust_segments);

    {
        int i;
        for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
            if (options->browse_dirs[i] != NULL && options->browse_dirs[i][0] != '\0') {
                config_set(cfg, "browse", browse_dir_keys[i], options->browse_dirs[i]);
            }
        }
    }

    ok = config_save(cfg, options->ini_path);
    config_destroy(cfg);
    return ok;
}

bool app_options_save_paths_only(const app_options *options)
{
    config *cfg;
    bool ok;
    int i;

    if (options == NULL || options->no_save_ini ||
            options->ini_path == NULL || options->ini_path[0] == '\0') {
        return true; /* nothing named to write to: silent no-op */
    }

    /* Re-read the file so we preserve every other setting, then overwrite only
       the [browse] path keys. */
    cfg = config_load(options->ini_path);
    if (cfg == NULL) {
        cfg = config_load(NULL);
    }
    if (cfg == NULL) {
        return false;
    }

    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        if (options->browse_dirs[i] != NULL && options->browse_dirs[i][0] != '\0') {
            config_set(cfg, "browse", browse_dir_keys[i], options->browse_dirs[i]);
        } else {
            config_remove_prefix(cfg, "browse", browse_dir_keys[i]);
        }
    }

    /* Current machine-media paths are persisted here too, so mounts selected in
       Misc -> Machine can be saved without changing other configuration. */
    config_write_rom_config(cfg, options);

    /* The snapshot folder is now [browse] snapshot; drop the legacy key. */
    config_remove_prefix(cfg, "state", "quicksave_folder");

    ok = config_save(cfg, options->ini_path);
    config_destroy(cfg);
    return ok;
}

void app_options_destroy(app_options *options)
{
    int i;

    if (options == NULL) {
        return;
    }

    free(options->ini_path);
    free(options->breakpoint);
    free(options->turbo_multipliers);
    free(options->symbol_files);
    free(options->video_standard);
    free(options->keyboard_joystick_layout);
    free(options->basic_path);
    free(options->sna_path);
    free(options->audio_record_path);
    free(options->assembler_file);
    free(options->assembler_address);
    free(options->assembler_run_address);
    free(options->disk_s6d0);
    free(options->disk_s6d1);
    free(options->hd_s7d0);
    free(options->hd_s5d0);
    for (i = 0; i < options->diskii_count; ++i) {
        free(options->diskii[i].path);
        options->diskii[i].path = NULL;
    }
    for (i = 0; i < options->smartport_count; ++i) {
        free(options->smartport[i].path);
        options->smartport[i].path = NULL;
    }
    for (i = 0; i < APP_BROWSE_DIR_COUNT; ++i) {
        free(options->browse_dirs[i]);
    }
    for (i = 0; i < A2M_DISK_SLOT_COUNT; ++i) {
        disk_slot_free(&options->disk_slots[i]);
    }

    memset(options, 0, sizeof(*options));
}

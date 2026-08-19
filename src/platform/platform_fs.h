#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLATFORM_FS_MAX_ENTRIES 4096
#define PLATFORM_FS_NAME_MAX 256

typedef struct platform_fs_entry {
    char name[PLATFORM_FS_NAME_MAX]; /* filename only, no path; "" is never stored */
    bool is_dir;
    uint64_t size; /* 0 for directories */
} platform_fs_entry;

typedef struct platform_fs_listing {
    platform_fs_entry entries[PLATFORM_FS_MAX_ENTRIES];
    int count;
    bool truncated; /* true if the directory had more than PLATFORM_FS_MAX_ENTRIES entries */
} platform_fs_listing;

/* Writes the process's current working directory into out. Returns false on failure. */
bool platform_fs_get_cwd(char *out, size_t out_size);

/* Lists dir_path directly (no chdir()). Entries are sorted directories-first,
   then case-insensitive alphabetical within each group. "." is skipped; ".."
   is kept when present so it can be navigated like any other row. Returns
   false if dir_path could not be opened as a directory. */
bool platform_fs_list_dir(const char *dir_path, platform_fs_listing *out);

/* Returns true if path exists and is a directory. */
bool platform_fs_is_dir(const char *path);

/* Joins dir and name with the platform path separator into out. */
void platform_fs_path_join(char *out, size_t out_size, const char *dir, const char *name);

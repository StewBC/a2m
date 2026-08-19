#include "platform_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#define A2M_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define a2m_getcwd _getcwd
#define a2m_stricmp _stricmp
#define PLATFORM_FS_SEPARATOR '\\'
#else
#define A2M_STAT_ISDIR(mode) S_ISDIR(mode)
#define a2m_getcwd getcwd
#define a2m_stricmp strcasecmp
#define PLATFORM_FS_SEPARATOR '/'
#endif

bool platform_fs_get_cwd(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    return a2m_getcwd(out, (int)out_size) != NULL;
}

bool platform_fs_is_dir(const char *path)
{
    struct stat st;

    return path != NULL && path[0] != '\0' && stat(path, &st) == 0 && A2M_STAT_ISDIR(st.st_mode);
}

void platform_fs_path_join(char *out, size_t out_size, const char *dir, const char *name)
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
        snprintf(out, out_size, "%s%c%s", dir, PLATFORM_FS_SEPARATOR, name != NULL ? name : "");
    }
}

static int platform_fs_entry_cmp(const void *pa, const void *pb)
{
    const platform_fs_entry *a = (const platform_fs_entry *)pa;
    const platform_fs_entry *b = (const platform_fs_entry *)pb;
    bool a_up = strcmp(a->name, "..") == 0;
    bool b_up = strcmp(b->name, "..") == 0;

    if (a_up != b_up) {
        return a_up ? -1 : 1;
    }
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    return a2m_stricmp(a->name, b->name);
}

static void platform_fs_add_entry(
    platform_fs_listing *out,
    const char *name,
    bool is_dir,
    uint64_t size)
{
    platform_fs_entry *entry;

    if (strcmp(name, ".") == 0) {
        return;
    }
    if (out->count >= PLATFORM_FS_MAX_ENTRIES) {
        out->truncated = true;
        return;
    }

    entry = &out->entries[out->count];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->is_dir = is_dir;
    entry->size = is_dir ? 0u : size;
    out->count++;
}

bool platform_fs_list_dir(const char *dir_path, platform_fs_listing *out)
{
    if (dir_path == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));

#if defined(_WIN32)
    {
        WIN32_FIND_DATAA data;
        HANDLE handle;
        char search_path[1024];
        uint64_t size;

        platform_fs_path_join(search_path, sizeof(search_path), dir_path, "*");
        handle = FindFirstFileA(search_path, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }

        do {
            bool is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            size = ((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow;
            platform_fs_add_entry(out, data.cFileName, is_dir, size);
        } while (FindNextFileA(handle, &data));

        FindClose(handle);
    }
#else
    {
        DIR *handle;
        struct dirent *entry;
        char path[1024];
        struct stat st;

        handle = opendir(dir_path);
        if (handle == NULL) {
            return false;
        }

        while ((entry = readdir(handle)) != NULL) {
            bool is_dir = false;
            uint64_t size = 0;

            platform_fs_path_join(path, sizeof(path), dir_path, entry->d_name);
            if (stat(path, &st) == 0) {
                is_dir = A2M_STAT_ISDIR(st.st_mode);
                size = is_dir || st.st_size < 0 ? 0u : (uint64_t)st.st_size;
            }
            platform_fs_add_entry(out, entry->d_name, is_dir, size);
        }

        closedir(handle);
    }
#endif

    qsort(out->entries, (size_t)out->count, sizeof(out->entries[0]), platform_fs_entry_cmp);
    return true;
}

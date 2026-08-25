#include "fs_watch_internal.h"

#if !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define FS_WATCH_WINDOWS_BUFFER_SIZE (32u * 1024u)

typedef struct fs_watch_windows {
    HANDLE root_handle;
    HANDLE parent_handle;
    HANDLE stop_event;
    OVERLAPPED root_overlapped;
    OVERLAPPED parent_overlapped;
    DWORD root_buffer[FS_WATCH_WINDOWS_BUFFER_SIZE / sizeof(DWORD)];
    DWORD parent_buffer[FS_WATCH_WINDOWS_BUFFER_SIZE / sizeof(DWORD)];
    wchar_t *root_path;
    wchar_t *parent_path;
    wchar_t *root_name;
    bool root_pending;
    bool parent_pending;
} fs_watch_windows;

static wchar_t *fs_watch_windows_utf8_to_wide(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *wide;

    if (count <= 0) {
        return NULL;
    }
    wide = malloc((size_t)count * sizeof(*wide));
    if (!wide || MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count) != count) {
        free(wide);
        return NULL;
    }
    return wide;
}

static wchar_t *fs_watch_windows_full_path(const char *root_path)
{
    wchar_t *input = fs_watch_windows_utf8_to_wide(root_path);
    wchar_t *full = NULL;
    DWORD count;

    if (!input) {
        return NULL;
    }
    count = GetFullPathNameW(input, 0, NULL, NULL);
    if (count != 0) {
        full = malloc((size_t)count * sizeof(*full));
        if (!full || GetFullPathNameW(input, count, full, NULL) == 0) {
            free(full);
            full = NULL;
        }
    }
    free(input);
    return full;
}

static wchar_t *fs_watch_windows_wide_copy(const wchar_t *text, size_t count)
{
    wchar_t *copy = malloc((count + 1u) * sizeof(*copy));
    if (copy) {
        memcpy(copy, text, count * sizeof(*copy));
        copy[count] = L'\0';
    }
    return copy;
}

static bool fs_watch_windows_split_root(fs_watch_windows *platform)
{
    size_t len = wcslen(platform->root_path);
    wchar_t *slash;

    while (len > 3u &&
           (platform->root_path[len - 1u] == L'\\' ||
            platform->root_path[len - 1u] == L'/')) {
        platform->root_path[--len] = L'\0';
    }
    if (len == 3u && platform->root_path[1] == L':' &&
        (platform->root_path[2] == L'\\' || platform->root_path[2] == L'/')) {
        return true; /* A drive root cannot be renamed within a parent. */
    }

    slash = wcsrchr(platform->root_path, L'\\');
    if (!slash) {
        slash = wcsrchr(platform->root_path, L'/');
    }
    if (!slash || slash[1] == L'\0') {
        return false;
    }
    platform->root_name = fs_watch_windows_wide_copy(slash + 1, wcslen(slash + 1));
    if (slash == platform->root_path + 2 && platform->root_path[1] == L':') {
        platform->parent_path = fs_watch_windows_wide_copy(platform->root_path, 3u);
    } else {
        platform->parent_path = fs_watch_windows_wide_copy(
            platform->root_path, (size_t)(slash - platform->root_path));
    }
    return platform->root_name && platform->parent_path;
}

static HANDLE fs_watch_windows_open_directory(const wchar_t *path)
{
    return CreateFileW(
        path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
}

static bool fs_watch_windows_submit(
    HANDLE handle,
    OVERLAPPED *overlapped,
    unsigned char *buffer,
    DWORD buffer_size,
    BOOL recursive,
    DWORD filter)
{
    DWORD ignored = 0;

    ResetEvent(overlapped->hEvent);
    if (ReadDirectoryChangesW(
            handle, buffer, buffer_size, recursive, filter,
            &ignored, overlapped, NULL)) {
        return true;
    }
    return GetLastError() == ERROR_IO_PENDING;
}

static void fs_watch_windows_cancel_and_wait(
    HANDLE handle,
    OVERLAPPED *overlapped,
    bool *pending)
{
    DWORD ignored = 0;

    if (handle == INVALID_HANDLE_VALUE || !*pending) {
        return;
    }
    (void)CancelIoEx(handle, overlapped);
    (void)GetOverlappedResult(handle, overlapped, &ignored, TRUE);
    *pending = false;
}

static bool fs_watch_windows_name_to_utf8(
    fs_watch *watch,
    const wchar_t *name,
    DWORD byte_count,
    char out_path[FS_WATCH_PATH_MAX])
{
    int wide_count;
    int utf8_count;
    int i;

    if ((byte_count % sizeof(wchar_t)) != 0 ||
        byte_count / sizeof(wchar_t) > (DWORD)INT_MAX) {
        fs_watch_require_rescan(watch);
        return false;
    }
    wide_count = (int)(byte_count / sizeof(wchar_t));
    utf8_count = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, name, wide_count,
        out_path, FS_WATCH_PATH_MAX - 1, NULL, NULL);
    if (utf8_count <= 0 || utf8_count >= FS_WATCH_PATH_MAX) {
        fs_watch_require_rescan(watch);
        return false;
    }
    out_path[utf8_count] = '\0';
    for (i = 0; i < utf8_count; ++i) {
        if (out_path[i] == '\\') {
            out_path[i] = '/';
        }
    }
    return true;
}

static bool fs_watch_windows_process_root(
    fs_watch *watch,
    const unsigned char *buffer,
    DWORD byte_count)
{
    DWORD offset = 0;

    while (offset < byte_count) {
        const FILE_NOTIFY_INFORMATION *info;
        char relative_path[FS_WATCH_PATH_MAX];
        uint32_t flags = 0;
        size_t minimum = offsetof(FILE_NOTIFY_INFORMATION, FileName);

        if ((size_t)(byte_count - offset) < minimum) {
            fs_watch_require_rescan(watch);
            return false;
        }
        info = (const FILE_NOTIFY_INFORMATION *)(buffer + offset);
        if (info->FileNameLength > byte_count - offset - minimum ||
            !fs_watch_windows_name_to_utf8(
                watch, info->FileName, info->FileNameLength, relative_path)) {
            return false;
        }

        switch (info->Action) {
        case FILE_ACTION_ADDED:
            flags = FS_WATCH_CREATE;
            break;
        case FILE_ACTION_REMOVED:
            flags = FS_WATCH_REMOVE;
            break;
        case FILE_ACTION_MODIFIED:
            flags = FS_WATCH_MODIFY | FS_WATCH_METADATA;
            break;
        case FILE_ACTION_RENAMED_OLD_NAME:
        case FILE_ACTION_RENAMED_NEW_NAME:
            flags = FS_WATCH_RENAME;
            break;
        default:
            fs_watch_require_rescan(watch);
            return false;
        }
        (void)fs_watch_emit(watch, flags, relative_path);

        if (info->NextEntryOffset == 0) {
            return true;
        }
        if (info->NextEntryOffset < minimum ||
            info->NextEntryOffset > byte_count - offset) {
            fs_watch_require_rescan(watch);
            return false;
        }
        offset += info->NextEntryOffset;
    }
    return true;
}

static bool fs_watch_windows_process_parent(
    fs_watch *watch,
    const unsigned char *buffer,
    DWORD byte_count)
{
    fs_watch_windows *platform = (fs_watch_windows *)watch->platform;
    DWORD offset = 0;
    size_t root_name_len = wcslen(platform->root_name);

    while (offset < byte_count) {
        const FILE_NOTIFY_INFORMATION *info;
        size_t minimum = offsetof(FILE_NOTIFY_INFORMATION, FileName);
        size_t name_len;

        if ((size_t)(byte_count - offset) < minimum) {
            fs_watch_require_rescan(watch);
            return false;
        }
        info = (const FILE_NOTIFY_INFORMATION *)(buffer + offset);
        if (info->FileNameLength > byte_count - offset - minimum ||
            (info->FileNameLength % sizeof(wchar_t)) != 0) {
            fs_watch_require_rescan(watch);
            return false;
        }
        name_len = info->FileNameLength / sizeof(wchar_t);
        if (name_len == root_name_len &&
            _wcsnicmp(info->FileName, platform->root_name, name_len) == 0 &&
            (info->Action == FILE_ACTION_REMOVED ||
             info->Action == FILE_ACTION_RENAMED_OLD_NAME)) {
            fs_watch_require_rescan(watch);
            return false;
        }
        if (info->NextEntryOffset == 0) {
            return true;
        }
        if (info->NextEntryOffset < minimum ||
            info->NextEntryOffset > byte_count - offset) {
            fs_watch_require_rescan(watch);
            return false;
        }
        offset += info->NextEntryOffset;
    }
    return true;
}

int fs_watch_platform_run(fs_watch *watch)
{
    static const DWORD root_filter =
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
    fs_watch_windows *platform = calloc(1, sizeof(*platform));
    HANDLE waits[3];
    DWORD wait_count;

    if (!platform) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }
    platform->root_handle = INVALID_HANDLE_VALUE;
    platform->parent_handle = INVALID_HANDLE_VALUE;
    watch->platform = platform;

    platform->root_path = fs_watch_windows_full_path(fs_watch_root_path(watch));
    if (!platform->root_path || !fs_watch_windows_split_root(platform)) {
        fs_watch_signal_startup(watch, false);
        goto done;
    }
    platform->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    platform->root_overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    platform->parent_overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    platform->root_handle = fs_watch_windows_open_directory(platform->root_path);
    if (!platform->stop_event || !platform->root_overlapped.hEvent ||
        !platform->parent_overlapped.hEvent ||
        platform->root_handle == INVALID_HANDLE_VALUE) {
        fs_watch_signal_startup(watch, false);
        goto done;
    }
    if (platform->parent_path) {
        platform->parent_handle = fs_watch_windows_open_directory(platform->parent_path);
        if (platform->parent_handle == INVALID_HANDLE_VALUE) {
            fs_watch_signal_startup(watch, false);
            goto done;
        }
    }
    if (!fs_watch_windows_submit(
            platform->root_handle, &platform->root_overlapped,
            (unsigned char *)platform->root_buffer,
            sizeof(platform->root_buffer), TRUE, root_filter)) {
        fs_watch_signal_startup(watch, false);
        goto done;
    }
    platform->root_pending = true;
    if (platform->parent_handle != INVALID_HANDLE_VALUE &&
        !fs_watch_windows_submit(
            platform->parent_handle, &platform->parent_overlapped,
            (unsigned char *)platform->parent_buffer,
            sizeof(platform->parent_buffer), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME)) {
        fs_watch_signal_startup(watch, false);
        goto done;
    }
    platform->parent_pending = platform->parent_handle != INVALID_HANDLE_VALUE;

    fs_watch_signal_startup(watch, true);
    waits[0] = platform->stop_event;
    waits[1] = platform->root_overlapped.hEvent;
    waits[2] = platform->parent_overlapped.hEvent;
    wait_count = platform->parent_handle == INVALID_HANDLE_VALUE ? 2u : 3u;
    while (!fs_watch_should_stop(watch)) {
        DWORD wait_result = WaitForMultipleObjects(wait_count, waits, FALSE, INFINITE);
        DWORD bytes = 0;
        bool ok;

        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result == WAIT_OBJECT_0 + 1u) {
            ok = GetOverlappedResult(
                platform->root_handle, &platform->root_overlapped, &bytes, FALSE) != 0;
            platform->root_pending = false;
            if (!ok || bytes == 0 ||
                !fs_watch_windows_process_root(
                    watch, (const unsigned char *)platform->root_buffer, bytes) ||
                !fs_watch_windows_submit(
                    platform->root_handle, &platform->root_overlapped,
                    (unsigned char *)platform->root_buffer,
                    sizeof(platform->root_buffer), TRUE,
                    root_filter)) {
                if (!fs_watch_should_stop(watch)) {
                    fs_watch_require_rescan(watch);
                }
                break;
            }
            platform->root_pending = true;
            continue;
        }
        if (wait_result == WAIT_OBJECT_0 + 2u) {
            ok = GetOverlappedResult(
                platform->parent_handle, &platform->parent_overlapped, &bytes, FALSE) != 0;
            platform->parent_pending = false;
            if (!ok || bytes == 0 ||
                !fs_watch_windows_process_parent(
                    watch, (const unsigned char *)platform->parent_buffer, bytes) ||
                !fs_watch_windows_submit(
                    platform->parent_handle, &platform->parent_overlapped,
                    (unsigned char *)platform->parent_buffer,
                    sizeof(platform->parent_buffer), FALSE,
                    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME)) {
                if (!fs_watch_should_stop(watch)) {
                    fs_watch_require_rescan(watch);
                }
                break;
            }
            platform->parent_pending = true;
            continue;
        }
        fs_watch_require_rescan(watch);
        break;
    }

done:
    fs_watch_windows_cancel_and_wait(
        platform->root_handle, &platform->root_overlapped,
        &platform->root_pending);
    fs_watch_windows_cancel_and_wait(
        platform->parent_handle, &platform->parent_overlapped,
        &platform->parent_pending);
    mutex_lock(watch->state_lock);
    {
        HANDLE root_handle = platform->root_handle;
        HANDLE parent_handle = platform->parent_handle;
        HANDLE stop_event = platform->stop_event;
        HANDLE root_event = platform->root_overlapped.hEvent;
        HANDLE parent_event = platform->parent_overlapped.hEvent;
        platform->root_handle = INVALID_HANDLE_VALUE;
        platform->parent_handle = INVALID_HANDLE_VALUE;
        platform->stop_event = NULL;
        platform->root_overlapped.hEvent = NULL;
        platform->parent_overlapped.hEvent = NULL;
        mutex_unlock(watch->state_lock);
        if (root_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(root_handle);
        }
        if (parent_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(parent_handle);
        }
        if (stop_event) {
            CloseHandle(stop_event);
        }
        if (root_event) {
            CloseHandle(root_event);
        }
        if (parent_event) {
            CloseHandle(parent_event);
        }
    }
    return 0;
}

void fs_watch_platform_request_stop(fs_watch *watch)
{
    fs_watch_windows *platform;

    if (!watch || !watch->state_lock) {
        return;
    }
    mutex_lock(watch->state_lock);
    platform = (fs_watch_windows *)watch->platform;
    if (platform) {
        if (platform->stop_event) {
            SetEvent(platform->stop_event);
        }
        if (platform->root_handle != INVALID_HANDLE_VALUE) {
            (void)CancelIoEx(platform->root_handle, NULL);
        }
        if (platform->parent_handle != INVALID_HANDLE_VALUE) {
            (void)CancelIoEx(platform->parent_handle, NULL);
        }
    }
    mutex_unlock(watch->state_lock);
}

bool fs_watch_platform_add_directory(fs_watch *watch, const char *relative_path)
{
    (void)relative_path;
    return watch && !fs_watch_should_stop(watch);
}

void fs_watch_platform_dispose(fs_watch *watch)
{
    fs_watch_windows *platform;

    if (!watch) {
        return;
    }
    platform = (fs_watch_windows *)watch->platform;
    if (!platform) {
        return;
    }
    free(platform->root_path);
    free(platform->parent_path);
    free(platform->root_name);
    free(platform);
    watch->platform = NULL;
}

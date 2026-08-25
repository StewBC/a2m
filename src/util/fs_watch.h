#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FS_WATCH_PATH_MAX 1024

typedef struct fs_watch fs_watch;

typedef enum fs_watch_flags {
    FS_WATCH_CREATE = 1u << 0,
    FS_WATCH_REMOVE = 1u << 1,
    FS_WATCH_MODIFY = 1u << 2,
    FS_WATCH_RENAME = 1u << 3,
    FS_WATCH_METADATA = 1u << 4,
    FS_WATCH_DIRECTORY = 1u << 5
} fs_watch_flags;

typedef struct fs_watch_event {
    uint32_t flags;
    char relative_path[FS_WATCH_PATH_MAX];
} fs_watch_event;

/*
 * Watch root_path and queue root-relative invalidations. Creation succeeds only
 * after the native root watch is armed. Events are hints: callers must verify
 * the affected filesystem state before mutating their cache.
 */
fs_watch *fs_watch_create(const char *root_path);

/* Required by non-recursive backends; an idempotent no-op elsewhere. */
bool fs_watch_add_directory(fs_watch *watch, const char *relative_path);

bool fs_watch_try_pop(fs_watch *watch, fs_watch_event *out_event);

/* Atomically take and clear the sticky notification-loss state. */
bool fs_watch_take_rescan_required(fs_watch *watch);

/* Request stop, wake/cancel the native wait, join, close, and free. */
void fs_watch_destroy(fs_watch *watch);

#if defined(FS_WATCH_TESTING)
fs_watch *fs_watch_test_create_inert(void);
size_t fs_watch_test_queue_capacity(void);
bool fs_watch_test_emit(
    fs_watch *watch,
    uint32_t flags,
    const char *relative_path);
void fs_watch_test_require_rescan(fs_watch *watch);
#endif

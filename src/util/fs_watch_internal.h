#pragma once

#include "fs_watch.h"

#include "cond.h"
#include "message_queue.h"
#include "mutex.h"

#include <stdbool.h>

#define FS_WATCH_QUEUE_CAPACITY 256u

typedef struct thread thread;

struct fs_watch {
    char *root_path;
    message_queue *events;
    mutex *state_lock;
    cond *state_changed;
    thread *worker;
    bool startup_done;
    bool startup_ok;
    bool stop_requested;
    bool rescan_required;
    void *platform;
};

const char *fs_watch_root_path(const fs_watch *watch);
bool fs_watch_relative_path_valid(const char *relative_path);
bool fs_watch_should_stop(fs_watch *watch);
void fs_watch_signal_startup(fs_watch *watch, bool ok);
bool fs_watch_emit(fs_watch *watch, uint32_t flags, const char *relative_path);
void fs_watch_require_rescan(fs_watch *watch);

int fs_watch_platform_run(fs_watch *watch);
void fs_watch_platform_request_stop(fs_watch *watch);
bool fs_watch_platform_add_directory(fs_watch *watch, const char *relative_path);
void fs_watch_platform_dispose(fs_watch *watch);

#include "fs_watch_internal.h"

#include "thread.h"

#include <stdlib.h>
#include <string.h>

static char *fs_watch_strdup(const char *text)
{
    size_t len;
    char *copy;

    if (!text) {
        return NULL;
    }
    len = strlen(text);
    copy = malloc(len + 1u);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len + 1u);
    return copy;
}

static int fs_watch_worker(void *userdata)
{
    return fs_watch_platform_run((fs_watch *)userdata);
}

static fs_watch *fs_watch_allocate(const char *root_path)
{
    fs_watch *watch = calloc(1, sizeof(*watch));

    if (!watch) {
        return NULL;
    }
    watch->root_path = fs_watch_strdup(root_path);
    watch->events = message_queue_create(
        sizeof(fs_watch_event), FS_WATCH_QUEUE_CAPACITY);
    watch->state_lock = mutex_create();
    watch->state_changed = cond_create();
    if (!watch->root_path || !watch->events || !watch->state_lock ||
        !watch->state_changed) {
        fs_watch_destroy(watch);
        return NULL;
    }
    return watch;
}

const char *fs_watch_root_path(const fs_watch *watch)
{
    return watch ? watch->root_path : NULL;
}

bool fs_watch_relative_path_valid(const char *relative_path)
{
    const char *part;

    if (!relative_path || relative_path[0] == '/' || relative_path[0] == '\\') {
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
        size_t len = end ? (size_t)(end - part) : strlen(part);
        if ((len == 1u && part[0] == '.') ||
            (len == 2u && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        if (!end) {
            break;
        }
        part = end + 1;
    }
    return strlen(relative_path) < FS_WATCH_PATH_MAX;
}

bool fs_watch_should_stop(fs_watch *watch)
{
    bool stop;

    mutex_lock(watch->state_lock);
    stop = watch->stop_requested;
    mutex_unlock(watch->state_lock);
    return stop;
}

void fs_watch_signal_startup(fs_watch *watch, bool ok)
{
    mutex_lock(watch->state_lock);
    if (!watch->startup_done) {
        watch->startup_ok = ok;
        watch->startup_done = true;
        cond_broadcast(watch->state_changed);
    }
    mutex_unlock(watch->state_lock);
}

void fs_watch_require_rescan(fs_watch *watch)
{
    if (!watch) {
        return;
    }
    mutex_lock(watch->state_lock);
    watch->rescan_required = true;
    mutex_unlock(watch->state_lock);
}

bool fs_watch_emit(fs_watch *watch, uint32_t flags, const char *relative_path)
{
    fs_watch_event event;
    size_t len;

    if (!watch || !relative_path) {
        return false;
    }
    len = strlen(relative_path);
    if (len >= sizeof(event.relative_path)) {
        fs_watch_require_rescan(watch);
        return false;
    }

    memset(&event, 0, sizeof(event));
    event.flags = flags;
    memcpy(event.relative_path, relative_path, len + 1u);
    if (!message_queue_push(watch->events, &event)) {
        fs_watch_require_rescan(watch);
        return false;
    }
    return true;
}

fs_watch *fs_watch_create(const char *root_path)
{
    fs_watch *watch;
    bool startup_ok;

    if (!root_path || root_path[0] == '\0') {
        return NULL;
    }

    watch = fs_watch_allocate(root_path);
    if (!watch) {
        return NULL;
    }

    watch->worker = thread_create("fs-watch", fs_watch_worker, watch);
    if (!watch->worker) {
        fs_watch_destroy(watch);
        return NULL;
    }

    mutex_lock(watch->state_lock);
    while (!watch->startup_done) {
        cond_wait(watch->state_changed, watch->state_lock);
    }
    startup_ok = watch->startup_ok;
    mutex_unlock(watch->state_lock);

    if (!startup_ok) {
        fs_watch_destroy(watch);
        return NULL;
    }
    return watch;
}

bool fs_watch_add_directory(fs_watch *watch, const char *relative_path)
{
    if (!watch || !fs_watch_relative_path_valid(relative_path)) {
        return false;
    }
    return fs_watch_platform_add_directory(watch, relative_path);
}

bool fs_watch_try_pop(fs_watch *watch, fs_watch_event *out_event)
{
    if (!watch || !out_event) {
        return false;
    }
    return message_queue_try_pop(watch->events, out_event);
}

bool fs_watch_take_rescan_required(fs_watch *watch)
{
    bool required;

    if (!watch) {
        return false;
    }
    mutex_lock(watch->state_lock);
    required = watch->rescan_required;
    watch->rescan_required = false;
    mutex_unlock(watch->state_lock);
    return required;
}

void fs_watch_destroy(fs_watch *watch)
{
    if (!watch) {
        return;
    }

    if (watch->state_lock) {
        mutex_lock(watch->state_lock);
        watch->stop_requested = true;
        mutex_unlock(watch->state_lock);
    }
    fs_watch_platform_request_stop(watch);
    if (watch->worker) {
        thread_join(watch->worker);
        thread_destroy(watch->worker);
    }
    fs_watch_platform_dispose(watch);
    cond_destroy(watch->state_changed);
    mutex_destroy(watch->state_lock);
    message_queue_destroy(watch->events);
    free(watch->root_path);
    free(watch);
}

size_t fs_watch_test_queue_capacity(void)
{
    return FS_WATCH_QUEUE_CAPACITY;
}

fs_watch *fs_watch_test_create_inert(void)
{
    return fs_watch_allocate(".");
}

bool fs_watch_test_emit(
    fs_watch *watch,
    uint32_t flags,
    const char *relative_path)
{
    return fs_watch_emit(watch, flags, relative_path);
}

void fs_watch_test_require_rescan(fs_watch *watch)
{
    fs_watch_require_rescan(watch);
}

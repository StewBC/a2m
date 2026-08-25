#include "fs_watch_internal.h"

#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/poll.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct fs_watch_linux_entry {
    int descriptor;
    char *relative_path;
} fs_watch_linux_entry;

typedef struct fs_watch_linux {
    int notify_fd;
    int wake_fd;
    fs_watch_linux_entry *entries;
    size_t entry_count;
    size_t entry_cap;
} fs_watch_linux;

static char *fs_watch_linux_strdup(const char *text)
{
    size_t len = strlen(text);
    char *copy = malloc(len + 1u);
    if (copy) {
        memcpy(copy, text, len + 1u);
    }
    return copy;
}

static char *fs_watch_linux_absolute_path(
    const fs_watch *watch,
    const char *relative_path)
{
    const char *root = fs_watch_root_path(watch);
    size_t root_len = strlen(root);
    size_t relative_len = strlen(relative_path);
    bool need_slash = root_len != 0 && root[root_len - 1u] != '/';
    char *path = malloc(root_len + (need_slash ? 1u : 0u) + relative_len + 1u);

    if (!path) {
        return NULL;
    }
    memcpy(path, root, root_len);
    if (need_slash && relative_len != 0) {
        path[root_len++] = '/';
    }
    memcpy(path + root_len, relative_path, relative_len + 1u);
    return path;
}

static bool fs_watch_linux_copy_directory(
    fs_watch *watch,
    int descriptor,
    char out_path[FS_WATCH_PATH_MAX],
    bool *out_is_root)
{
    fs_watch_linux *platform = (fs_watch_linux *)watch->platform;
    size_t i;
    bool found = false;

    mutex_lock(watch->state_lock);
    if (platform) {
        for (i = 0; i < platform->entry_count; ++i) {
            if (platform->entries[i].descriptor == descriptor) {
                size_t len = strlen(platform->entries[i].relative_path);
                if (len < FS_WATCH_PATH_MAX) {
                    memcpy(out_path, platform->entries[i].relative_path, len + 1u);
                    *out_is_root = len == 0;
                    found = true;
                }
                break;
            }
        }
    }
    mutex_unlock(watch->state_lock);
    return found;
}

static void fs_watch_linux_remove_descriptor(fs_watch *watch, int descriptor)
{
    fs_watch_linux *platform = (fs_watch_linux *)watch->platform;
    size_t i;

    mutex_lock(watch->state_lock);
    if (platform) {
        for (i = 0; i < platform->entry_count; ++i) {
            if (platform->entries[i].descriptor == descriptor) {
                free(platform->entries[i].relative_path);
                platform->entry_count--;
                if (i != platform->entry_count) {
                    platform->entries[i] = platform->entries[platform->entry_count];
                }
                break;
            }
        }
    }
    mutex_unlock(watch->state_lock);
}

static void fs_watch_linux_process_event(
    fs_watch *watch,
    const struct inotify_event *native_event)
{
    char directory[FS_WATCH_PATH_MAX];
    char relative_path[FS_WATCH_PATH_MAX];
    bool is_root = false;
    uint32_t flags = 0;
    size_t directory_len;
    size_t name_len = 0;

    if ((native_event->mask & IN_Q_OVERFLOW) != 0) {
        fs_watch_require_rescan(watch);
        return;
    }
    if (!fs_watch_linux_copy_directory(
            watch, native_event->wd, directory, &is_root)) {
        if ((native_event->mask & IN_IGNORED) == 0 &&
            !fs_watch_should_stop(watch)) {
            fs_watch_require_rescan(watch);
        }
        return;
    }

    if ((native_event->mask & IN_IGNORED) != 0) {
        fs_watch_linux_remove_descriptor(watch, native_event->wd);
        if (is_root && !fs_watch_should_stop(watch)) {
            fs_watch_require_rescan(watch);
        }
        return;
    }
    if ((native_event->mask & (IN_MOVE_SELF | IN_UNMOUNT)) != 0) {
        fs_watch_require_rescan(watch);
        return;
    }

    directory_len = strlen(directory);
    if (native_event->len != 0) {
        name_len = strnlen(native_event->name, native_event->len);
    }
    if (directory_len + (directory_len && name_len ? 1u : 0u) + name_len >=
        sizeof(relative_path)) {
        fs_watch_require_rescan(watch);
        return;
    }
    memcpy(relative_path, directory, directory_len);
    if (directory_len && name_len) {
        relative_path[directory_len++] = '/';
    }
    memcpy(relative_path + directory_len, native_event->name, name_len);
    relative_path[directory_len + name_len] = '\0';

    if ((native_event->mask & IN_DELETE_SELF) != 0) {
        if (is_root) {
            fs_watch_require_rescan(watch);
        } else {
            (void)fs_watch_emit(
                watch, FS_WATCH_REMOVE | FS_WATCH_DIRECTORY, relative_path);
        }
        return;
    }

    if ((native_event->mask & IN_ISDIR) != 0) {
        flags |= FS_WATCH_DIRECTORY;
        if ((native_event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) != 0) {
            /* The watches follow directory inodes; their cached paths do not. */
            fs_watch_require_rescan(watch);
            return;
        }
    }
    if ((native_event->mask & IN_CREATE) != 0) {
        flags |= FS_WATCH_CREATE;
    }
    if ((native_event->mask & IN_DELETE) != 0) {
        flags |= FS_WATCH_REMOVE;
    }
    if ((native_event->mask & (IN_MODIFY | IN_CLOSE_WRITE)) != 0) {
        flags |= FS_WATCH_MODIFY;
    }
    if ((native_event->mask & IN_ATTRIB) != 0) {
        flags |= FS_WATCH_METADATA;
    }
    if ((native_event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) != 0) {
        flags |= FS_WATCH_RENAME;
    }
    if (flags != 0) {
        (void)fs_watch_emit(watch, flags, relative_path);
    }
}

bool fs_watch_platform_add_directory(fs_watch *watch, const char *relative_path)
{
    static const uint32_t mask =
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
        IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT |
        IN_ONLYDIR;
    fs_watch_linux *platform;
    fs_watch_linux_entry *grown;
    char *absolute_path;
    char *relative_copy;
    int descriptor;
    size_t i;

    if (!watch || !relative_path) {
        return false;
    }
    absolute_path = fs_watch_linux_absolute_path(watch, relative_path);
    relative_copy = fs_watch_linux_strdup(relative_path);
    if (!absolute_path || !relative_copy) {
        free(absolute_path);
        free(relative_copy);
        fs_watch_require_rescan(watch);
        return false;
    }

    mutex_lock(watch->state_lock);
    platform = (fs_watch_linux *)watch->platform;
    if (!platform || platform->notify_fd < 0 || watch->stop_requested) {
        mutex_unlock(watch->state_lock);
        free(absolute_path);
        free(relative_copy);
        return false;
    }
    if (platform->entry_count == platform->entry_cap) {
        size_t new_cap = platform->entry_cap ? platform->entry_cap * 2u : 16u;
        grown = realloc(platform->entries, new_cap * sizeof(*grown));
        if (!grown) {
            mutex_unlock(watch->state_lock);
            free(absolute_path);
            free(relative_copy);
            fs_watch_require_rescan(watch);
            return false;
        }
        platform->entries = grown;
        platform->entry_cap = new_cap;
    }

    descriptor = inotify_add_watch(platform->notify_fd, absolute_path, mask);
    free(absolute_path);
    if (descriptor < 0) {
        mutex_unlock(watch->state_lock);
        free(relative_copy);
        fs_watch_require_rescan(watch);
        return false;
    }
    for (i = 0; i < platform->entry_count;) {
        if (strcmp(platform->entries[i].relative_path, relative_path) == 0 &&
            platform->entries[i].descriptor != descriptor) {
            int old_descriptor = platform->entries[i].descriptor;
            free(platform->entries[i].relative_path);
            platform->entry_count--;
            if (i != platform->entry_count) {
                platform->entries[i] = platform->entries[platform->entry_count];
            }
            (void)inotify_rm_watch(platform->notify_fd, old_descriptor);
            continue;
        }
        ++i;
    }
    for (i = 0; i < platform->entry_count; ++i) {
        if (platform->entries[i].descriptor == descriptor) {
            free(platform->entries[i].relative_path);
            platform->entries[i].relative_path = relative_copy;
            mutex_unlock(watch->state_lock);
            return true;
        }
    }
    platform->entries[platform->entry_count].descriptor = descriptor;
    platform->entries[platform->entry_count].relative_path = relative_copy;
    platform->entry_count++;
    mutex_unlock(watch->state_lock);
    return true;
}

int fs_watch_platform_run(fs_watch *watch)
{
    fs_watch_linux *platform = calloc(1, sizeof(*platform));
    struct pollfd poll_fds[2];
    union {
        struct inotify_event align;
        unsigned char bytes[64u * 1024u];
    } buffer;

    if (!platform) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }
    platform->notify_fd = -1;
    platform->wake_fd = -1;
    watch->platform = platform;

    platform->notify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    platform->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (platform->notify_fd < 0 || platform->wake_fd < 0 ||
        !fs_watch_platform_add_directory(watch, "")) {
        fs_watch_signal_startup(watch, false);
        goto done;
    }
    fs_watch_signal_startup(watch, true);

    poll_fds[0].fd = platform->notify_fd;
    poll_fds[0].events = POLLIN;
    poll_fds[1].fd = platform->wake_fd;
    poll_fds[1].events = POLLIN;
    while (!fs_watch_should_stop(watch)) {
        int poll_result = poll(poll_fds, 2, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            fs_watch_require_rescan(watch);
            break;
        }
        if ((poll_fds[1].revents & POLLIN) != 0) {
            uint64_t value;
            (void)read(platform->wake_fd, &value, sizeof(value));
            break;
        }
        if ((poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fs_watch_require_rescan(watch);
            break;
        }
        if ((poll_fds[0].revents & POLLIN) != 0) {
            ssize_t count;
            while ((count = read(platform->notify_fd, buffer.bytes,
                                  sizeof(buffer.bytes))) > 0) {
                size_t offset = 0;
                while (offset + sizeof(struct inotify_event) <= (size_t)count) {
                    const struct inotify_event *event =
                        (const struct inotify_event *)(buffer.bytes + offset);
                    size_t event_size = sizeof(*event) + event->len;
                    if (event_size > (size_t)count - offset) {
                        fs_watch_require_rescan(watch);
                        break;
                    }
                    fs_watch_linux_process_event(watch, event);
                    offset += event_size;
                }
            }
            if (count < 0 && errno != EAGAIN && errno != EINTR) {
                fs_watch_require_rescan(watch);
                break;
            }
        }
    }

done:
    mutex_lock(watch->state_lock);
    {
        int notify_fd = platform->notify_fd;
        int wake_fd = platform->wake_fd;
        platform->notify_fd = -1;
        platform->wake_fd = -1;
        mutex_unlock(watch->state_lock);
        if (notify_fd >= 0) {
            close(notify_fd);
        }
        if (wake_fd >= 0) {
            close(wake_fd);
        }
    }
    return 0;
}

void fs_watch_platform_request_stop(fs_watch *watch)
{
    fs_watch_linux *platform;
    uint64_t value = 1;

    if (!watch || !watch->state_lock) {
        return;
    }
    mutex_lock(watch->state_lock);
    platform = (fs_watch_linux *)watch->platform;
    if (platform && platform->wake_fd >= 0) {
        (void)write(platform->wake_fd, &value, sizeof(value));
    }
    mutex_unlock(watch->state_lock);
}

void fs_watch_platform_dispose(fs_watch *watch)
{
    fs_watch_linux *platform;
    size_t i;

    if (!watch) {
        return;
    }
    platform = (fs_watch_linux *)watch->platform;
    if (!platform) {
        return;
    }
    for (i = 0; i < platform->entry_count; ++i) {
        free(platform->entries[i].relative_path);
    }
    free(platform->entries);
    free(platform);
    watch->platform = NULL;
}

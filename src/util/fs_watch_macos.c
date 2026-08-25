#include "fs_watch_internal.h"

#include <CoreServices/CoreServices.h>

#include <stdlib.h>
#include <string.h>

typedef struct fs_watch_macos {
    FSEventStreamRef stream;
    CFRunLoopRef run_loop;
    char *absolute_root;
    size_t root_len;
} fs_watch_macos;

static void fs_watch_macos_callback(
    ConstFSEventStreamRef stream,
    void *context,
    size_t event_count,
    void *event_paths,
    const FSEventStreamEventFlags event_flags[],
    const FSEventStreamEventId event_ids[])
{
    fs_watch *watch = (fs_watch *)context;
    fs_watch_macos *platform = (fs_watch_macos *)watch->platform;
    const char **paths = (const char **)event_paths;
    size_t i;

    (void)stream;
    (void)event_ids;
    if (!platform) {
        fs_watch_require_rescan(watch);
        return;
    }

    for (i = 0; i < event_count; ++i) {
        FSEventStreamEventFlags native_flags = event_flags[i];
        uint32_t flags = 0;
        const char *relative_path;

        if ((native_flags & (kFSEventStreamEventFlagMustScanSubDirs |
                             kFSEventStreamEventFlagUserDropped |
                             kFSEventStreamEventFlagKernelDropped |
                             kFSEventStreamEventFlagEventIdsWrapped |
                             kFSEventStreamEventFlagRootChanged |
                             kFSEventStreamEventFlagMount |
                             kFSEventStreamEventFlagUnmount)) != 0) {
            fs_watch_require_rescan(watch);
            continue;
        }

        /* FSEvents does not promise both rename paths. Never leave an old
           parent cached on the strength of a single ambiguous half. */
        if ((native_flags & kFSEventStreamEventFlagItemRenamed) != 0) {
            fs_watch_require_rescan(watch);
            continue;
        }

        if (strcmp(paths[i], platform->absolute_root) == 0) {
            relative_path = "";
        } else if (strncmp(paths[i], platform->absolute_root,
                           platform->root_len) == 0 &&
                   paths[i][platform->root_len] == '/') {
            relative_path = paths[i] + platform->root_len + 1u;
        } else {
            fs_watch_require_rescan(watch);
            continue;
        }

        if ((native_flags & kFSEventStreamEventFlagItemCreated) != 0) {
            flags |= FS_WATCH_CREATE;
        }
        if ((native_flags & kFSEventStreamEventFlagItemRemoved) != 0) {
            flags |= FS_WATCH_REMOVE;
        }
        if ((native_flags & kFSEventStreamEventFlagItemModified) != 0) {
            flags |= FS_WATCH_MODIFY;
        }
        if ((native_flags & (kFSEventStreamEventFlagItemInodeMetaMod |
                             kFSEventStreamEventFlagItemFinderInfoMod |
                             kFSEventStreamEventFlagItemChangeOwner |
                             kFSEventStreamEventFlagItemXattrMod)) != 0) {
            flags |= FS_WATCH_METADATA;
        }
        if ((native_flags & kFSEventStreamEventFlagItemIsDir) != 0) {
            flags |= FS_WATCH_DIRECTORY;
        }
        if (flags != 0) {
            (void)fs_watch_emit(watch, flags, relative_path);
        }
    }
}

int fs_watch_platform_run(fs_watch *watch)
{
    fs_watch_macos *platform = calloc(1, sizeof(*platform));
    FSEventStreamContext context;
    CFStringRef root_string = NULL;
    CFArrayRef paths = NULL;
    const void *path_values[1];
    FSEventStreamCreateFlags create_flags;
    bool started = false;

    if (!platform) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }
    watch->platform = platform;

    platform->absolute_root = realpath(fs_watch_root_path(watch), NULL);
    if (!platform->absolute_root) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }
    platform->root_len = strlen(platform->absolute_root);
    root_string = CFStringCreateWithFileSystemRepresentation(
        kCFAllocatorDefault, platform->absolute_root);
    if (!root_string) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }
    path_values[0] = root_string;
    paths = CFArrayCreate(
        kCFAllocatorDefault, path_values, 1, &kCFTypeArrayCallBacks);
    if (!paths) {
        CFRelease(root_string);
        fs_watch_signal_startup(watch, false);
        return 0;
    }

    memset(&context, 0, sizeof(context));
    context.info = watch;
    create_flags = kFSEventStreamCreateFlagWatchRoot |
                   kFSEventStreamCreateFlagFileEvents |
                   kFSEventStreamCreateFlagNoDefer;
    platform->stream = FSEventStreamCreate(
        kCFAllocatorDefault,
        fs_watch_macos_callback,
        &context,
        paths,
        kFSEventStreamEventIdSinceNow,
        0.05,
        create_flags);
    CFRelease(paths);
    CFRelease(root_string);
    if (!platform->stream) {
        fs_watch_signal_startup(watch, false);
        return 0;
    }

    platform->run_loop = CFRunLoopGetCurrent();
    CFRetain(platform->run_loop);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    FSEventStreamScheduleWithRunLoop(
        platform->stream, platform->run_loop, kCFRunLoopDefaultMode);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    started = FSEventStreamStart(platform->stream);
    if (!started) {
        fs_watch_signal_startup(watch, false);
    } else {
        fs_watch_signal_startup(watch, true);
        while (!fs_watch_should_stop(watch)) {
            (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
        }
        FSEventStreamStop(platform->stream);
    }

    FSEventStreamInvalidate(platform->stream);
    FSEventStreamRelease(platform->stream);
    platform->stream = NULL;
    mutex_lock(watch->state_lock);
    {
        CFRunLoopRef run_loop = platform->run_loop;
        platform->run_loop = NULL;
        mutex_unlock(watch->state_lock);
        CFRelease(run_loop);
    }
    return 0;
}

void fs_watch_platform_request_stop(fs_watch *watch)
{
    fs_watch_macos *platform;
    CFRunLoopRef run_loop = NULL;

    if (!watch || !watch->state_lock) {
        return;
    }
    mutex_lock(watch->state_lock);
    platform = (fs_watch_macos *)watch->platform;
    if (platform && platform->run_loop) {
        run_loop = platform->run_loop;
        CFRetain(run_loop);
    }
    mutex_unlock(watch->state_lock);

    if (run_loop) {
        CFRunLoopStop(run_loop);
        CFRunLoopWakeUp(run_loop);
        CFRelease(run_loop);
    }
}

bool fs_watch_platform_add_directory(fs_watch *watch, const char *relative_path)
{
    (void)relative_path;
    return watch && !fs_watch_should_stop(watch);
}

void fs_watch_platform_dispose(fs_watch *watch)
{
    fs_watch_macos *platform;

    if (!watch) {
        return;
    }
    platform = (fs_watch_macos *)watch->platform;
    if (!platform) {
        return;
    }
    free(platform->absolute_root);
    free(platform);
    watch->platform = NULL;
}

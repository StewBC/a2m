#include "fs_watch.h"

#include <SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define TEST_MKDIR(path) _mkdir(path)
#define TEST_RMDIR(path) _rmdir(path)
#else
#include <unistd.h>
#define TEST_MKDIR(path) mkdir(path, 0755)
#define TEST_RMDIR(path) rmdir(path)
#endif

#define TEST_ROOT "test_fs_watch_root"
#define TEST_MOVED_ROOT "test_fs_watch_root_moved"
#define TEST_TIMEOUT_MS 5000u

typedef enum wait_result {
    WAIT_RESULT_TIMEOUT = 0,
    WAIT_RESULT_EVENT,
    WAIT_RESULT_RESCAN
} wait_result;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static void remove_test_tree(void)
{
    (void)remove(TEST_ROOT "/sub/nested.txt");
    (void)remove(TEST_ROOT "/created.txt");
    (void)remove(TEST_ROOT "/renamed.txt");
    (void)TEST_RMDIR(TEST_ROOT "/sub");
    (void)TEST_RMDIR(TEST_ROOT);
    (void)remove(TEST_MOVED_ROOT "/sub/nested.txt");
    (void)remove(TEST_MOVED_ROOT "/created.txt");
    (void)remove(TEST_MOVED_ROOT "/renamed.txt");
    (void)TEST_RMDIR(TEST_MOVED_ROOT "/sub");
    (void)TEST_RMDIR(TEST_MOVED_ROOT);
}

static void write_file(const char *path, const char *mode, const char *text)
{
    FILE *file = fopen(path, mode);
    size_t len = strlen(text);

    if (!file) {
        fail("open file for write");
    }
    if (fwrite(text, 1, len, file) != len) {
        fclose(file);
        fail("write file");
    }
    if (fclose(file) != 0) {
        fail("close written file");
    }
}

static wait_result wait_for_event(
    fs_watch *watch,
    const char *path_a,
    const char *path_b,
    uint32_t required_flags)
{
    uint32_t started = SDL_GetTicks();

    while ((uint32_t)(SDL_GetTicks() - started) < TEST_TIMEOUT_MS) {
        fs_watch_event event;
        while (fs_watch_try_pop(watch, &event)) {
            bool path_matches = strcmp(event.relative_path, path_a) == 0 ||
                (path_b && strcmp(event.relative_path, path_b) == 0);
            if (path_matches && (event.flags & required_flags) != 0) {
                return WAIT_RESULT_EVENT;
            }
        }
        if (fs_watch_take_rescan_required(watch)) {
            return WAIT_RESULT_RESCAN;
        }
        SDL_Delay(10);
    }
    return WAIT_RESULT_TIMEOUT;
}

static void settle_after_rescan_signal(fs_watch *watch)
{
    uint32_t started = SDL_GetTicks();
    uint32_t last_activity = started;

    while ((uint32_t)(SDL_GetTicks() - started) < TEST_TIMEOUT_MS &&
           (uint32_t)(SDL_GetTicks() - last_activity) < 200u) {
        fs_watch_event event;
        bool activity = false;
        while (fs_watch_try_pop(watch, &event)) {
            activity = true;
        }
        if (fs_watch_take_rescan_required(watch)) {
            activity = true;
        }
        if (activity) {
            last_activity = SDL_GetTicks();
        }
        SDL_Delay(10);
    }
}

static void expect_event(
    fs_watch *watch,
    const char *path,
    uint32_t required_flags,
    const char *message)
{
    wait_result result = wait_for_event(watch, path, NULL, required_flags);
    if (result != WAIT_RESULT_EVENT) {
        fprintf(stderr, "watch wait for %s flags=0x%08x ended with %d\n",
            path, (unsigned)required_flags, (int)result);
        fail(message);
    }
}

static void expect_event_or_rescan(
    fs_watch *watch,
    const char *path,
    uint32_t required_flags,
    const char *message)
{
    if (wait_for_event(watch, path, NULL, required_flags) == WAIT_RESULT_TIMEOUT) {
        fail(message);
    }
}

static void test_native_events(void)
{
    fs_watch *watch;
    wait_result renamed;

    remove_test_tree();
    if (TEST_MKDIR(TEST_ROOT) != 0 && errno != EEXIST) {
        fail("create test root");
    }
    watch = fs_watch_create(TEST_ROOT);
    if (!watch) {
        fail("create watcher");
    }
    if (!fs_watch_add_directory(watch, "") ||
        !fs_watch_add_directory(watch, "")) {
        fail("root add must be idempotent");
    }
    if (fs_watch_add_directory(watch, "../outside")) {
        fail("directory add accepted a parent traversal");
    }

    write_file(TEST_ROOT "/created.txt", "wb", "one");
    expect_event(
        watch, "created.txt", FS_WATCH_CREATE,
        "create did not produce an invalidation");

    write_file(TEST_ROOT "/created.txt", "ab", "-two");
    expect_event(
        watch, "created.txt", FS_WATCH_MODIFY | FS_WATCH_METADATA,
        "modify did not produce an invalidation");

    if (rename(TEST_ROOT "/created.txt", TEST_ROOT "/renamed.txt") != 0) {
        fail("rename file");
    }
    renamed = wait_for_event(
        watch, "created.txt", "renamed.txt", FS_WATCH_RENAME);
    if (renamed == WAIT_RESULT_TIMEOUT) {
        fail("rename produced neither paths nor rescan state");
    }
    settle_after_rescan_signal(watch);

    if (TEST_MKDIR(TEST_ROOT "/sub") != 0) {
        fail("create subdirectory");
    }
    expect_event(
        watch, "sub", FS_WATCH_CREATE,
        "directory create did not produce an invalidation");
    if (!fs_watch_add_directory(watch, "sub") ||
        !fs_watch_add_directory(watch, "sub")) {
        fail("subdirectory add must be idempotent");
    }

    write_file(TEST_ROOT "/sub/nested.txt", "wb", "nested");
    expect_event(
        watch, "sub/nested.txt", FS_WATCH_CREATE,
        "nested create did not produce an invalidation");

    if (remove(TEST_ROOT "/renamed.txt") != 0) {
        fail("remove renamed file");
    }
    expect_event_or_rescan(
        watch, "renamed.txt", FS_WATCH_REMOVE,
        "delete produced neither a path nor rescan state");

    settle_after_rescan_signal(watch);
    if (rename(TEST_ROOT, TEST_MOVED_ROOT) != 0) {
        fail("rename watched root");
    }
    if (wait_for_event(watch, "", NULL, FS_WATCH_RENAME) != WAIT_RESULT_RESCAN) {
        fail("watched-root move did not require a rescan");
    }

    fs_watch_destroy(watch);
    remove_test_tree();
}

static void drain_events(fs_watch *watch)
{
    fs_watch_event event;
    while (fs_watch_try_pop(watch, &event)) {
    }
}

static void test_loss_state(void)
{
    fs_watch *watch;
    size_t i;
    size_t capacity;

    watch = fs_watch_test_create_inert();
    if (!watch) {
        fail("create inert overflow watcher");
    }

    capacity = fs_watch_test_queue_capacity();
    for (i = 0; i < capacity; ++i) {
        if (!fs_watch_test_emit(watch, FS_WATCH_MODIFY, "synthetic")) {
            fail("synthetic queue filled early");
        }
    }
    if (fs_watch_test_emit(watch, FS_WATCH_MODIFY, "overflow")) {
        fail("overflow event unexpectedly fit");
    }
    if (!fs_watch_take_rescan_required(watch) ||
        fs_watch_take_rescan_required(watch)) {
        fail("overflow loss state is not sticky-and-take");
    }
    drain_events(watch);

    {
        char too_long[FS_WATCH_PATH_MAX + 1u];
        memset(too_long, 'x', sizeof(too_long) - 1u);
        too_long[sizeof(too_long) - 1u] = '\0';
        if (fs_watch_test_emit(watch, FS_WATCH_MODIFY, too_long) ||
            !fs_watch_take_rescan_required(watch)) {
            fail("unrepresentable path did not require a rescan");
        }
    }

    fs_watch_test_require_rescan(watch);
    if (!fs_watch_take_rescan_required(watch)) {
        fail("first explicit loss missing");
    }
    fs_watch_test_require_rescan(watch);
    if (!fs_watch_take_rescan_required(watch)) {
        fail("second loss was cleared with the first");
    }

    fs_watch_destroy(watch);
}

int main(void)
{
    test_native_events();
    test_loss_state();
    printf("fs_watch: all tests passed\n");
    return 0;
}

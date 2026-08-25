#include "fs_watch_internal.h"

int fs_watch_platform_run(fs_watch *watch)
{
    fs_watch_signal_startup(watch, false);
    return 0;
}

void fs_watch_platform_request_stop(fs_watch *watch)
{
    (void)watch;
}

bool fs_watch_platform_add_directory(fs_watch *watch, const char *relative_path)
{
    (void)watch;
    (void)relative_path;
    return false;
}

void fs_watch_platform_dispose(fs_watch *watch)
{
    (void)watch;
}

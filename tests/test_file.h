#ifndef A2M_TEST_FILE_H
#define A2M_TEST_FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

static int a2m_test_write_temp_file(
    char *path,
    size_t path_size,
    const char *prefix,
    const char *source)
{
    FILE *file;

#if defined(_WIN32)
    char temp_dir[MAX_PATH];
    char temp_path[MAX_PATH];

    if (GetTempPathA((DWORD)sizeof(temp_dir), temp_dir) == 0 ||
        GetTempFileNameA(temp_dir, prefix, 0, temp_path) == 0 ||
        strlen(temp_path) + 1 > path_size) {
        fprintf(stderr, "failed to create temporary file path\n");
        return 1;
    }
    snprintf(path, path_size, "%s", temp_path);
#else
    int fd;

    snprintf(path, path_size, "/tmp/%s_XXXXXX", prefix);
    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    file = fdopen(fd, "w");
    if (file == NULL) {
        perror("fdopen");
        close(fd);
        remove(path);
        return 1;
    }
    fputs(source, file);
    if (fclose(file) != 0) {
        perror("fclose");
        remove(path);
        return 1;
    }
    return 0;
#endif

    file = fopen(path, "w");
    if (file == NULL) {
        perror("fopen");
        remove(path);
        return 1;
    }
    fputs(source, file);
    if (fclose(file) != 0) {
        perror("fclose");
        remove(path);
        return 1;
    }
    return 0;
}

static void a2m_test_remove_file(const char *path)
{
    remove(path);
}

#endif

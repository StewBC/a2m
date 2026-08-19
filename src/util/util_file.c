#include "util_file.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define A2M_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define A2M_STAT_ISREG(mode) S_ISREG(mode)
#endif

void util_file_close(UTIL_FILE *f)
{
    if (f && f->is_used && f->is_file_open && f->fp) {
        fclose(f->fp);
        f->fp = NULL;
        f->is_file_open = 0;
    }
}

void util_file_discard(UTIL_FILE *f)
{
    if (!f || !f->is_used) {
        return;
    }
    if (f->is_file_open && f->fp) {
        fclose(f->fp);
        f->fp = NULL;
        f->is_file_open = 0;
    }
    if (f->is_file_loaded) {
        free(f->file_data);
        f->file_data = NULL;
        f->is_file_loaded = 0;
    }
    free(f->file_path);
    free(f->file_mode);
    f->file_path = NULL;
    f->file_mode = NULL;
    f->file_display_name = NULL;
    f->file_size = 0;
    f->is_used = 0;
}

int util_file_open(UTIL_FILE *f, const char *file_name, const char *file_mode)
{
    struct stat st;
    char *slash;

    if (!f || !file_name || !file_mode) {
        return A2_ERR;
    }

    util_file_discard(f);

    f->file_path = strdup(file_name);
    f->file_mode = strdup(file_mode);
    if (!f->file_path || !f->file_mode) {
        util_file_discard(f);
        return A2_ERR;
    }

    slash = strrchr(f->file_path, '/');
#ifdef _WIN32
    {
        char *b = strrchr(f->file_path, '\\');
        if (b && (!slash || b > slash)) {
            slash = b;
        }
    }
#endif
    f->file_display_name = slash ? slash + 1 : f->file_path;
    f->is_used = 1;

    if (stat(file_name, &st) == 0 && A2M_STAT_ISREG(st.st_mode)) {
        f->file_size = (int64_t)st.st_size;
    } else if (file_mode[0] != 'w') {
        util_file_discard(f);
        return A2_ERR;
    }

    f->fp = fopen(file_name, file_mode);
    if (!f->fp) {
        util_file_discard(f);
        return A2_ERR;
    }
    f->is_file_open = 1;
    return A2_OK;
}

int util_file_load(UTIL_FILE *f, const char *file_name, const char *file_mode)
{
    size_t want;
    size_t got;
    int64_t total_size;

    if (util_file_open(f, file_name, file_mode) != A2_OK) {
        return A2_ERR;
    }

    total_size = f->file_size + (int64_t)f->load_padding;
    if (total_size < 0) {
        util_file_discard(f);
        return A2_ERR;
    }

    f->file_data = (char *)malloc((size_t)total_size);
    if (!f->file_data) {
        util_file_discard(f);
        return A2_ERR;
    }

    want = (size_t)f->file_size;
    got = want > 0 ? fread(f->file_data, 1, want, f->fp) : 0;
    f->file_size = (int64_t)got;
    if ((size_t)total_size > got) {
        memset(f->file_data + got, 0, (size_t)total_size - got);
    }

    f->is_file_loaded = 1;
    fclose(f->fp);
    f->fp = NULL;
    f->is_file_open = 0;
    return A2_OK;
}

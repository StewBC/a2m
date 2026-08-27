// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#include <limits.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <ctype.h>
#include <stdlib.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char *asm_strdup(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if(out) {
        memcpy(out, s, len + 1);
    }
    return out;
}

static int is_absolute_path(const char *path) {
    return path && (path[0] == '/'
#if defined(_WIN32)
        || path[0] == '\\' ||
        (isalpha((unsigned char)path[0]) && path[1] == ':' &&
         (path[2] == '/' || path[2] == '\\'))
#endif
    );
}

static const char *find_last_path_separator(const char *path) {
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if(!slash || (backslash && backslash > slash)) {
        return backslash;
    }
#endif
    return slash;
}

static char *canonicalize_path(const char *path) {
    char resolved[PATH_MAX];
#if defined(_WIN32)
    if(_fullpath(resolved, path, sizeof(resolved))) {
        return asm_strdup(resolved);
    }
#else
    if(realpath(path, resolved)) {
        return asm_strdup(resolved);
    }
#endif
    return asm_strdup(path);
}

static int path_is_openable_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if(!fp) {
        return 0;
    }
    fclose(fp);
    return 1;
}

int file_path_is_directory(const char *path) {
    struct stat st;
    if(!path || !*path || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

/* Join directory + relative path, inserting a separator when needed. */
static char *join_dir_path(const char *dir, const char *path) {
    size_t dir_len = strlen(dir);
    size_t path_len = strlen(path);
    int need_sep = 0;
    if(dir_len > 0) {
        char last = dir[dir_len - 1];
        need_sep = !(last == '/' || last == '\\');
    }
    char *combined = malloc(dir_len + (size_t)need_sep + path_len + 1);
    if(!combined) {
        return NULL;
    }
    memcpy(combined, dir, dir_len);
    if(need_sep) {
        combined[dir_len] = '/';
        memcpy(combined + dir_len + 1, path, path_len + 1);
    } else {
        memcpy(combined + dir_len, path, path_len + 1);
    }
    return combined;
}

static char *join_current_dir_path(ASSEMBLER *as, const char *path) {
    FILE_FRAME *top = file_stack_top(as);
    if(!top || !top->file || !top->file->display_name) {
        return canonicalize_path(path);
    }

    const char *base = top->file->display_name;
    const char *slash = find_last_path_separator(base);
    if(!slash) {
        return canonicalize_path(path);
    }

    size_t dir_len = (size_t)(slash - base);
    char *dir = malloc(dir_len + 1);
    if(!dir) {
        return NULL;
    }
    memcpy(dir, base, dir_len);
    dir[dir_len] = '\0';

    char *combined = join_dir_path(dir, path);
    free(dir);
    if(!combined) {
        return NULL;
    }
    char *canonical = canonicalize_path(combined);
    free(combined);
    return canonical;
}

char *file_resolve_against_current(ASSEMBLER *as, const char *path) {
    if(!path || !*path) {
        return NULL;
    }
    if(is_absolute_path(path) || !file_stack_top(as)) {
        return canonicalize_path(path);
    }
    return join_current_dir_path(as, path);
}

void file_search_dirs_clear(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->search_dirs.items; i++) {
        free(*AM65_ARRAY_GET(&as->search_dirs, char *, i));
    }
    as->search_dirs.items = 0;
}

int file_search_dir_add(ASSEMBLER *as, char *resolved_dir) {
    if(!as || !resolved_dir) {
        return ASM_ERR;
    }
    for(size_t i = 0; i < as->search_dirs.items; i++) {
        char *existing = *AM65_ARRAY_GET(&as->search_dirs, char *, i);
        if(existing && strcmp(existing, resolved_dir) == 0) {
            free(resolved_dir);
            return ASM_OK;
        }
    }
    if(ASM_OK != AM65_ARRAY_ADD(&as->search_dirs, resolved_dir)) {
        free(resolved_dir);
        return ASM_ERR;
    }
    return ASM_OK;
}

void file_seed_search_dirs_clear(ASSEMBLER *as) {
    if(!as) {
        return;
    }
    for(size_t i = 0; i < as->seed_search_dirs.items; i++) {
        free(*AM65_ARRAY_GET(&as->seed_search_dirs, char *, i));
    }
    as->seed_search_dirs.items = 0;
}

int file_seed_search_dir_add(ASSEMBLER *as, char *resolved_dir) {
    if(!as || !resolved_dir) {
        return ASM_ERR;
    }
    for(size_t i = 0; i < as->seed_search_dirs.items; i++) {
        char *existing = *AM65_ARRAY_GET(&as->seed_search_dirs, char *, i);
        if(existing && strcmp(existing, resolved_dir) == 0) {
            free(resolved_dir);
            return ASM_OK;
        }
    }
    if(ASM_OK != AM65_ARRAY_ADD(&as->seed_search_dirs, resolved_dir)) {
        free(resolved_dir);
        return ASM_ERR;
    }
    return ASM_OK;
}

int file_search_dirs_reset_from_seed(ASSEMBLER *as) {
    if(!as) {
        return ASM_ERR;
    }
    file_search_dirs_clear(as);
    for(size_t i = 0; i < as->seed_search_dirs.items; i++) {
        char *seed = *AM65_ARRAY_GET(&as->seed_search_dirs, char *, i);
        if(!seed) {
            continue;
        }
        char *copy = asm_strdup(seed);
        if(!copy || ASM_OK != file_search_dir_add(as, copy)) {
            free(copy);
            return ASM_ERR;
        }
    }
    return ASM_OK;
}

char *file_format_open_miss(ASSEMBLER *as, const char *kind, const char *path) {
    char buf[ASM_ERR_MAX_STR_LEN];
    size_t used = 0;
    int n = snprintf(buf, sizeof(buf), "Unable to open %s file: %s", kind, path);
    if(n < 0) {
        return asm_strdup("Unable to open file");
    }
    used = (size_t)n;
    if(used >= sizeof(buf)) {
        used = sizeof(buf) - 1;
    }

    if(as && as->search_dirs.items > 0 && used + 9 < sizeof(buf)) {
        memcpy(buf + used, " (tried:", 8);
        used += 8;
        buf[used] = '\0';

        char *primary = file_resolve_against_current(as, path);
        if(primary && used + 1 + strlen(primary) + 1 < sizeof(buf)) {
            buf[used++] = ' ';
            memcpy(buf + used, primary, strlen(primary) + 1);
            used += strlen(primary);
        }
        free(primary);

        for(size_t i = 0; i < as->search_dirs.items; i++) {
            char *dir = *AM65_ARRAY_GET(&as->search_dirs, char *, i);
            if(!dir) {
                continue;
            }
            char *candidate = join_dir_path(dir, path);
            if(!candidate) {
                continue;
            }
            char *canonical = canonicalize_path(candidate);
            free(candidate);
            if(!canonical) {
                continue;
            }
            size_t clen = strlen(canonical);
            if(used + 2 + clen + 1 >= sizeof(buf)) {
                free(canonical);
                break;
            }
            buf[used++] = ',';
            buf[used++] = ' ';
            memcpy(buf + used, canonical, clen + 1);
            used += clen;
            free(canonical);
        }
        if(used + 1 < sizeof(buf)) {
            buf[used++] = ')';
            buf[used] = '\0';
        }
    }
    return asm_strdup(buf);
}

char *file_resolve_path(ASSEMBLER *as, const char *path) {
    if(!path || !*path) {
        return NULL;
    }

    if(is_absolute_path(path) || !file_stack_top(as)) {
        return canonicalize_path(path);
    }

    char *primary = join_current_dir_path(as, path);
    if(!primary) {
        return NULL;
    }
    if(path_is_openable_file(primary)) {
        return primary;
    }

    for(size_t i = 0; i < as->search_dirs.items; i++) {
        char *dir = *AM65_ARRAY_GET(&as->search_dirs, char *, i);
        if(!dir) {
            continue;
        }
        char *combined = join_dir_path(dir, path);
        if(!combined) {
            free(primary);
            return NULL;
        }
        char *canonical = canonicalize_path(combined);
        free(combined);
        if(!canonical) {
            free(primary);
            return NULL;
        }
        if(path_is_openable_file(canonical)) {
            free(primary);
            return canonical;
        }
        free(canonical);
    }

    free(primary);
    return NULL;
}

static ASM_FILE *find_loaded_file(ASSEMBLER *as, const char *display_name) {
    for(size_t i = 0; i < as->files.items; i++) {
        ASM_FILE *f = *AM65_ARRAY_GET(&as->files, ASM_FILE*, i);
        if(f && f->display_name && strcmp(f->display_name, display_name) == 0) {
            return f;
        }
    }
    return NULL;
}

static int is_recursive_include(ASSEMBLER *as, const char *display_name) {
    for(size_t i = 0; i < as->file_stack.items; i++) {
        FILE_FRAME *frame = AM65_ARRAY_GET(&as->file_stack, FILE_FRAME, i);
        if(frame && frame->file && frame->file->display_name &&
           strcmp(frame->file->display_name, display_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int load_file_into_array(ASSEMBLER *as, const char *display_name, ASM_FILE **out_file) {
    FILE *fp = fopen(display_name, "rb");
    if(!fp) {
        asm_err(as, ASM_ERR_FATAL, "Unable to open include file: %s", display_name);
        return ASM_ERR;
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        asm_err(as, ASM_ERR_FATAL, "Unable to seek file: %s", display_name);
        return ASM_ERR;
    }
    long size = ftell(fp);
    if(size < 0) {
        fclose(fp);
        asm_err(as, ASM_ERR_FATAL, "Unable to determine file size: %s", display_name);
        return ASM_ERR;
    }
    if(fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        asm_err(as, ASM_ERR_FATAL, "Unable to rewind file: %s", display_name);
        return ASM_ERR;
    }

    char *buf = malloc((size_t)size + 1);
    if(!buf) {
        fclose(fp);
        asm_err(as, ASM_ERR_FATAL, "Out of memory loading file: %s", display_name);
        return ASM_ERR;
    }

    size_t bytes_read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if(bytes_read != (size_t)size) {
        free(buf);
        asm_err(as, ASM_ERR_FATAL, "Unable to read file: %s", display_name);
        return ASM_ERR;
    }
    buf[bytes_read] = '\0';

    ASM_FILE *f = malloc(sizeof(*f));
    if(!f) {
        free(buf);
        asm_err(as, ASM_ERR_FATAL, "Out of memory loading file: %s", display_name);
        return ASM_ERR;
    }

    memset(f, 0, sizeof(*f));
    f->display_name = asm_strdup(display_name);
    f->buf = buf;
    f->size = bytes_read;
    if(!f->display_name) {
        free(f);
        free(buf);
        asm_err(as, ASM_ERR_FATAL, "Out of memory loading file: %s", display_name);
        return ASM_ERR;
    }

    if(ASM_OK != AM65_ARRAY_ADD(&as->files, f)) {
        free(f->display_name);
        free(f);
        free(buf);
        asm_err(as, ASM_ERR_FATAL, "Out of memory tracking file: %s", display_name);
        return ASM_ERR;
    }

    *out_file = f;
    return ASM_OK;
}

int file_load(ASSEMBLER *as, const char *path) {
    if(!as || !path) {
        return ASM_ERR;
    }

    char *display_name = file_resolve_path(as, path);
    if(!display_name) {
        /* Pass 1 records the miss; pass 2 would only duplicate the message. */
        if(as->pass == 1) {
            char *detail = file_format_open_miss(as, "include", path);
            if(detail) {
                asm_err(as, ASM_ERR_FATAL, "%s", detail);
                free(detail);
            } else {
                asm_err(as, ASM_ERR_FATAL, "Unable to open include file: %s", path);
            }
        }
        return ASM_ERR;
    }

    if(is_recursive_include(as, display_name)) {
        asm_err(as, ASM_ERR_FATAL, "Recursive include: %s", display_name);
        free(display_name);
        return ASM_ERR;
    }

    ASM_FILE *f = find_loaded_file(as, display_name);
    if(!f) {
        if(as->pass == 2) {
            asm_err(as, ASM_ERR_FATAL, "File was not loaded on pass 1: %s", display_name);
            free(display_name);
            return ASM_ERR;
        }
        if(ASM_OK != load_file_into_array(as, display_name, &f)) {
            free(display_name);
            return ASM_ERR;
        }
    }

    free(display_name);

    if(!as->root_file) {
        as->root_file = f;
    }
    return file_stack_push(as, f, f->buf, 0, 0);
}

int file_stack_push(ASSEMBLER *as, ASM_FILE *f, const char *read_ptr, size_t line_num, int is_macro) {
    if(!as || !f || !read_ptr) {
        return ASM_ERR;
    }

    FILE_FRAME frame;
    frame.file = f;
    frame.read_ptr = read_ptr;
    frame.line_num = line_num;
    frame.is_macro = is_macro;

    if(ASM_OK != AM65_ARRAY_ADD(&as->file_stack, frame)) {
        asm_err(as, ASM_ERR_FATAL, "Out of memory pushing file frame");
        return ASM_ERR;
    }
    as->current_file = f;
    as->current_file_name = f->display_name;
    as->current_line = line_num;
    return ASM_OK;
}

FILE_FRAME *file_stack_top(ASSEMBLER *as) {
    if(!as || as->file_stack.items == 0) {
        return NULL;
    }
    return AM65_ARRAY_GET(&as->file_stack, FILE_FRAME, as->file_stack.items - 1);
}

void file_stack_pop(ASSEMBLER *as) {
    if(!as || as->file_stack.items == 0) {
        return;
    }

    as->file_stack.items--;
    FILE_FRAME *top = file_stack_top(as);
    if(top) {
        as->current_file = top->file;
        as->current_file_name = top->file ? top->file->display_name : NULL;
        as->current_line = top->line_num;
    } else {
        as->current_file = NULL;
        as->current_file_name = NULL;
        as->current_line = 0;
    }
}

int file_read_line(ASSEMBLER *as) {
    FILE_FRAME *frame = file_stack_top(as);
    if(!as || !frame || !frame->read_ptr || *frame->read_ptr == '\0') {
        return 0;
    }

    const char *start = frame->read_ptr;
    const char *p = start;
    while(*p && *p != '\n' && *p != '\r') {
        p++;
    }

    size_t len = (size_t)(p - start);
    int too_long = len >= ASM_MAX_LINE;
    size_t copy_len = too_long ? ASM_MAX_LINE - 1 : len;
    memcpy(as->line, start, copy_len);
    as->line[copy_len] = '\0';
    as->line_len = (int)copy_len;

    while(*p && *p != '\n' && *p != '\r') {
        p++;
    }
    if(*p == '\r' && p[1] == '\n') {
        p += 2;
    } else if(*p == '\r' || *p == '\n') {
        p++;
    }

    frame->read_ptr = p;
    frame->line_num++;
    as->current_file = frame->file;
    as->current_file_name = frame->file ? frame->file->display_name : NULL;
    as->current_line = frame->line_num;

    if(too_long) {
        asm_err(as, ASM_ERR_FATAL, "Line too long; truncated to %d characters", ASM_MAX_LINE - 1);
    }
    return 1;
}

int file_stack_reset_for_pass2(ASSEMBLER *as) {
    if(!as || !as->root_file) {
        return ASM_ERR;
    }
    as->file_stack.items = 0;
    return file_stack_push(as, as->root_file, as->root_file->buf, 0, 0);
}

void files_free(ASSEMBLER *as) {
    if(!as) {
        return;
    }

    for(size_t i = 0; i < as->files.items; i++) {
        ASM_FILE *f = *AM65_ARRAY_GET(&as->files, ASM_FILE*, i);
        if(!f) {
            continue;
        }
        free(f->display_name);
        free(f->buf);
        free(f);
    }
    am65_array_free(&as->files);
    am65_array_free(&as->file_stack);
    as->root_file = NULL;
    as->current_file = NULL;
    as->current_file_name = NULL;
    as->current_line = 0;
}

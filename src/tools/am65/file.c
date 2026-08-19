// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

#include <limits.h>
#if defined(_WIN32)
#include <ctype.h>
#include <stdlib.h>
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

char *file_resolve_path(ASSEMBLER *as, const char *path) {
    if(!path || !*path) {
        return NULL;
    }

    if(is_absolute_path(path) || !file_stack_top(as)) {
        return canonicalize_path(path);
    }

    const char *base = file_stack_top(as)->file->display_name;
    const char *slash = find_last_path_separator(base);
    if(!slash) {
        return canonicalize_path(path);
    }

    size_t dir_len = (size_t)(slash - base + 1);
    size_t path_len = strlen(path);
    char *combined = malloc(dir_len + path_len + 1);
    if(!combined) {
        return NULL;
    }
    memcpy(combined, base, dir_len);
    memcpy(combined + dir_len, path, path_len + 1);

    char *canonical = canonicalize_path(combined);
    free(combined);
    return canonical;
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
        asm_err(as, ASM_ERR_FATAL, "Unable to resolve path: %s", path);
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

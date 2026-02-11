// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

#ifdef _WIN32
#include <sys/stat.h>
#define stat _stat64
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif // S_ISREG
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif // _WIN32

// UTF-8 <-> UTF-16 helpers
static int utf8_to_wide(const char *u8, wchar_t **out_w) {
    *out_w = NULL;
    if(!u8) {
        return EINVAL;
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8, -1, NULL, 0);
    if(n <= 0) {
        return EINVAL;
    }
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(*w));
    if(!w) {
        return ENOMEM;
    }
    if(!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8, -1, w, n)) {
        free(w);
        return EINVAL;
    }
    *out_w = w;
    return 0;
}

static int wide_to_utf8(const wchar_t *w, char *out, size_t out_sz) {
    if(!w || !out || out_sz == 0) {
        return EINVAL;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)out_sz, NULL, NULL);
    return n > 0 ? 0 : EINVAL;
}

// stat() that accepts UTF-8 path
static int fs_stat_utf8(const char *path, struct _stat64 *st) {
    wchar_t *wpath = NULL;
    int cvt = utf8_to_wide(path, &wpath);
    if(cvt) {
        return -cvt;
    }
    int rc = _wstat64(wpath, st);
    free(wpath);
    return rc == 0 ? 0 : -(errno ? errno : EIO);
}

#else
#include <errno.h>
static int fs_stat_utf8(const char *path, struct stat *st) {
    return (stat(path, st) == 0) ? 0 : -(errno ? errno : EIO);
}
#endif

int util_console_open(CONSOLE_MODE *console_mode) {
#ifdef _WIN32
    if(*console_mode != CONSOLE_NONE) {
        return A2_OK;
    }

    if(!AllocConsole()) {
        return A2_ERR;
    }
    *console_mode = CONSOLE_NEW;

    // Route CRT stdio to the console
    FILE *f;
    freopen_s(&f, "CONIN$",  "r", stdin);
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    Sleep(100);
#endif // _WIN32
    return A2_OK;
}

void util_console_close(CONSOLE_MODE console_mode) {
#ifdef _WIN32
    if(console_mode == CONSOLE_NEW) {
        FreeConsole();
    }
#endif // _WIN32
}

// Directory operations
int util_dir_change(const char *path) {
#ifdef _WIN32
    wchar_t *wpath = NULL;
    int cvt = utf8_to_wide(path, &wpath);
    if(cvt) {
        return A2_ERR;
    }
    int ok = (_wchdir(wpath) == 0);
    free(wpath);
    return ok ? A2_OK : A2_ERR;
#else // ndef _WIN32
    return (chdir(path) == 0) ? A2_OK : A2_ERR;
#endif // _WIN32
}

int util_dir_get_current(char *buffer, size_t buffer_size) {
#ifdef _WIN32
    if(!buffer || buffer_size == 0) {
        return A2_ERR;
    }
    // Get wide cwd first
    DWORD need = GetCurrentDirectoryW(0, NULL);
    if(!need) {
        return A2_ERR;
    }
    wchar_t *wbuf = (wchar_t *)malloc((size_t)need * sizeof(*wbuf));
    if(!wbuf) {
        return A2_ERR;
    }
    DWORD got = GetCurrentDirectoryW(need, wbuf);
    int rc = (got > 0 && wide_to_utf8(wbuf, buffer, buffer_size) == 0) ? A2_OK : A2_ERR;
    free(wbuf);
    return rc;
#else // ndef _WIN32
    return (getcwd(buffer, buffer_size) != NULL) ? A2_OK : A2_ERR;
#endif // _WIN32
}

// Enumerate current directory (".") into FILE_INFO entries
int util_dir_load_contents(DYNARRAY *array) {
#ifdef _WIN32
    WIN32_FIND_DATAW fdata;
    HANDLE h = INVALID_HANDLE_VALUE;

    // Build pattern ".\*"
    wchar_t pat[4] = L".\\*";
    h = FindFirstFileW(pat, &fdata);
    if(h == INVALID_HANDLE_VALUE) {
        return A2_ERR;
    }

    do {
        FILE_INFO info;
        memset(&info, 0, sizeof(info));

        // name (UTF-8)
        if(wide_to_utf8(fdata.cFileName, info.name, sizeof(info.name)) != 0) {
            continue;
        }
        info.name[PATH_MAX - 1] = '\0';
        info.name_length = (int)strlen(info.name);

        // stat for size + mtime + type
        struct _stat64 st;
        if(fs_stat_utf8(info.name, &st) == 0) {
            info.is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
            info.is_file = S_ISREG(st.st_mode) ? 1 : 0;
            info.size = info.is_file ? (size_t)st.st_size : 0u;
            info.mtime_sec = (uint64_t)st.st_mtime;
        } else {
            // Fall back to attributes if stat failed (FILETIME->mtime will == 0)
            info.is_directory = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            info.is_file = info.is_directory ? 0 : 1;
            LARGE_INTEGER li;
            li.LowPart = fdata.nFileSizeLow;
            li.HighPart = fdata.nFileSizeHigh;
            info.size = info.is_file ? (size_t)li.QuadPart : 0u;
        }

        ARRAY_ADD(array, info);
    } while(FindNextFileW(h, &fdata) != 0);

    DWORD err = GetLastError();
    FindClose(h);
    return (err == ERROR_NO_MORE_FILES) ? A2_OK : A2_ERR;

#else // ndef _WIN32
    DIR *dp = opendir(".");
    if(!dp) {
        return A2_ERR;
    }

    struct dirent *entry;
    while((entry = readdir(dp)) != NULL) {

        FILE_INFO info;
        memset(&info, 0, sizeof(info));
        strncpy(info.name, entry->d_name, PATH_MAX - 1);
        info.name[PATH_MAX - 1] = '\0';
        info.name_length = (int)strlen(info.name);

        struct stat st;
        if(stat(entry->d_name, &st) == 0) {
            info.is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
            info.is_file      = S_ISREG(st.st_mode) ? 1 : 0;
            info.size         = info.is_file ? (size_t)st.st_size : 0u;
            info.mtime_sec    = (uint64_t)st.st_mtime;
        }
        ARRAY_ADD(array, info);
    }
    closedir(dp);
    return A2_OK;
#endif // _WIN32
}

// File Operations
void util_file_close(UTIL_FILE *f) {
    if(f && f->is_used && f->is_file_open && f->fp) {
        fclose(f->fp);
        f->fp = NULL;
        f->is_file_open = 0;
    }
}

void util_file_discard(UTIL_FILE *f) {
    // If this handle is being reused, clean it up so it is "fresh"
    if(f->is_used) {
        if(f->is_file_open) {
            fclose(f->fp);
            f->fp = 0;
            f->is_file_open = 0;
        }
        if(f->is_file_loaded) {
            free(f->file_data);
            f->is_file_loaded = 0;
        }
        f->file_size = 0;
        f->file_display_name = 0;
        free(f->file_path);
        f->file_path = 0;
        free(f->file_mode);
        f->file_mode = 0;
        f->is_used = 0;
    }
}

int util_file_load(UTIL_FILE *f, const char *file_name, const char *file_mode) {
    if(A2_OK != util_file_open(f, file_name, file_mode)) {
        return A2_ERR;
    }

    int64_t total_size = f->file_size + (int64_t)f->load_padding;
    if(total_size < 0) {
        util_file_discard(f);
        return A2_ERR;
    }

    f->file_data = (char *)malloc((size_t)total_size);
    if(!f->file_data) {
        util_file_discard(f);
        return A2_ERR;
    }

    // Read exactly file_size bytes
    size_t want = (size_t)f->file_size;
    size_t got  = (want > 0) ? fread(f->file_data, 1, want, f->fp) : 0;
    f->file_size = (int64_t)got;

    // Zero the padding (safe even if padding==0)
    if((size_t)total_size > got) {
        memset(&f->file_data[got], 0, (size_t)total_size - got);
    }

    f->is_file_loaded = 1;
    fclose(f->fp);
    f->fp = NULL;
    f->is_file_open = 0;
    return A2_OK;
}

int util_file_open(UTIL_FILE *f, const char *file_name, const char *file_mode) {
    if(!f || !file_name || !file_mode) {
        return A2_ERR;
    }

    util_file_discard(f); // fresh

    // Keep original UTF-8 path & mode around
    f->file_path = strdup(file_name);
    f->file_mode = strdup(file_mode);
    f->file_display_name = (char *)util_strrtok(f->file_path, "\\/");
    f->file_display_name = f->file_display_name ? (f->file_display_name + 1) : f->file_path;
    f->is_used = 1;

    // Get size (and validate regular file) via stat
    uint64_t fsz = 0, mt = 0;
    if(util_file_stat_regular_utf8(file_name, &fsz, &mt) != 0) {
        if(file_mode[0] != 'w') {
            util_file_discard(f);
            return A2_ERR;
        }
    }
    f->file_size = (int64_t)fsz;

#ifdef _WIN32
    // Open with _wfopen so UTF-8 paths work
    wchar_t *wpath = NULL, *wmode = NULL;
    if(utf8_to_wide(file_name, &wpath) != 0 || utf8_to_wide(file_mode, &wmode) != 0) {
        free(wpath);
        free(wmode);
        util_file_discard(f);
        return A2_ERR;
    }
    f->fp = _wfopen(wpath, wmode);
    free(wpath);
    free(wmode);
#else  // ndef _WIN32
    f->fp = fopen(file_name, file_mode);
#endif  // _WIN32

    if(!f->fp) {
        util_file_discard(f);
        return A2_ERR;
    }

    f->is_used = 1;
    f->is_file_open = 1;
    return A2_OK;
}

int util_file_stat_regular_utf8(const char *path, uint64_t *size_out, uint64_t *mtime_out) {
    struct stat st;
    int rc = fs_stat_utf8(path, &st);
    if(rc != 0) {
        return rc;
    }
    if(!S_ISREG(st.st_mode)) {
        return -EINVAL;
    }
    if(size_out) {
        *size_out  = (uint64_t)st.st_size;
    }
    if(mtime_out) {
        *mtime_out = (uint64_t)st.st_mtime;
    }
    return 0;
}

// ini file helpers (SQW Make an ini object)
char *util_ini_find_character(char **start, char character) {
    char *old_start = *start;
    while(**start && **start != character && **start != ';') {
        (*start)++;
    }
    if(**start) {
        **start = '\0';
        char *tail = *start - 1;
        while(tail > old_start && isspace(*tail)) {
            *tail-- = '\0';
        }
        (*start)++;
    }
    return old_start;
}

char *util_ini_get_line(char **start, char *end) {
    char *old_start = *start;
    while(*start < end && **start != '\n' && **start != '\r') {
        (*start)++;
    }
    *(*start)++ = '\0'; // padding makes sure this will be safe
    while(*start < end && (**start == '\n' || **start == '\r')) {
        (*start)++;
    }
    return old_start;
}

char *util_ini_next_token(char **start) {
    while(**start && isspace(**start)) {
        (*start)++;
    }
    return *start;
}

int util_ini_load_file(const char *filename, INI_PAIR_CALLBACK callback, void *user_data) {
    UTIL_FILE ini_file;
    memset(&ini_file, 0, sizeof(UTIL_FILE));
    ini_file.load_padding = 1;
    char *section = 0;
    char *key = 0;
    char *value = 0;
    if(A2_OK != util_file_load(&ini_file, filename, "r")) {
        return A2_ERR;
    }
    char *current = ini_file.file_data;
    char *end = current + ini_file.file_size;
    while(current < end) {
        char *line = util_ini_get_line(&current, end);
        line = util_ini_next_token(&line);
        switch(*line) {
            case ';':
                continue;
            case '[':
                line++;
                util_ini_next_token(&line);
                if(*line) {
                    section = util_ini_find_character(&line, ']');
                }
                break;
            default:
                if(*line) {
                    key = util_ini_find_character(&line, '=');
                    if(!key) {
                        continue;
                    }
                    util_ini_next_token(&line);
                    value = util_ini_find_character(&line, '\0');
                    callback(user_data, section, key, value);
                }
                break;
        }
    }
    util_file_discard(&ini_file);
    return A2_OK;
}

int util_ini_save_file(const char *filename, INI_STORE *ini_store) {
    if(!ini_store || !ini_store->sections.items) {
        return A2_OK;
    }
    UTIL_FILE ini_file;
    int rval = A2_OK;
    memset(&ini_file, 0, sizeof(UTIL_FILE));
    ini_file.load_padding = 1;
    char *section = 0;
    char *key = 0;
    char *value = 0;
    if(A2_OK != util_file_open(&ini_file, filename, "w")) {
        return A2_ERR;
    }
    for(int sidx = 0; sidx < ini_store->sections.items; sidx++) {
        INI_SECTION *s = ARRAY_GET(&ini_store->sections, INI_SECTION, sidx);
        if(0 > fprintf(ini_file.fp, "[%s]\n", s->name)) {
            rval = A2_ERR;
            break;
        }
        for(int kvidx = 0; kvidx < s->kv.items; kvidx++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, kvidx);
            if(0 > fprintf(ini_file.fp, "%s = %s\n", kv->key, kv->val)) {
                rval = A2_ERR;
                break;
            }
        }
        if(rval == A2_ERR || 0 > fprintf(ini_file.fp, "\n")) {
            rval = A2_ERR;
            break;
        }
    }
    util_file_discard(&ini_file);
    return rval;
}

// Misc helpers
int util_file_info_qsort_cmp(const void *p1, const void *p2) {
    FILE_INFO *fip1 = (FILE_INFO *) p1;
    FILE_INFO *fip2 = (FILE_INFO *) p2;
    if(fip1->is_directory != fip2->is_directory) {
        return fip2->is_directory - fip1->is_directory;
    }
    return stricmp(fip1->name, fip2->name);
}

const char *util_strinstr(const char *haystack, const char *needle, int needle_length) {
    if(needle_length) {
        char c = tolower(*haystack);
        char search = tolower(*needle);

        while(c) {
            if(c == search) {
                int index = 0;
                char s1, h1;
                do {
                    index++;
                    s1 = tolower(needle[index]);
                    h1 = tolower(haystack[index]);
                } while(index < needle_length && h1 == s1);
                if(index == needle_length) {
                    break;
                }
            }
            haystack++;
            c = tolower(*haystack);
        }
    }
    return *haystack ? haystack : NULL;
}

const char *util_strrtok(const char *str, const char *delim) {
    const char *s = str + strlen(str);
    while(s > str) {
        const char c = *--s;
        const char *d = delim;
        while(*d && c != *d) {
            d++;
        }
        if(*d) {
            return s;
        }
    }
    return NULL;
}

char *util_strtok_r(char *s, const char *delim, char **ctx) {
#ifdef _WIN32
    return strtok_s(s, delim, ctx);
#else
    return strtok_r(s, delim, ctx);
#endif
}

int util_path_needs_quotes(const char *path) {
    if(!path) {
        return 0;
    }
    for(const char *p = path; *p; p++) {
        if(*p == ',' || isspace((unsigned char)*p)) {
            return 1;
        }
    }
    return 0;
}

static inline int util_path_is_sep(char c) {
    return c == '/' || c == '\\';
}

static int util_path_root_len(const char *p) {
#ifdef _WIN32
    if(!p || !*p) {
        return 0;
    }
    if(util_path_is_sep(p[0]) && util_path_is_sep(p[1])) {
        // UNC: \\server\share\...
        const char *s = p + 2;
        while(*s && !util_path_is_sep(*s)) {
            s++;
        }
        if(!*s) {
            return 0;
        }
        s++; // skip separator
        while(*s && !util_path_is_sep(*s)) {
            s++;
        }
        return (int)(s - p);
    }
    if(isalpha((unsigned char)p[0]) && p[1] == ':') {
        return 2;
    }
    return 0;
#else
    return (p && p[0] == '/') ? 1 : 0;
#endif
}

static int util_path_split_components(char *p, char **out, int max_out) {
    int count = 0;
    while(p && *p) {
        while(*p && util_path_is_sep(*p)) {
            p++;
        }
        if(!*p) {
            break;
        }
        if(count >= max_out) {
            return count;
        }
        out[count++] = p;
        while(*p && !util_path_is_sep(*p)) {
            p++;
        }
        if(*p) {
            *p++ = '\0';
        }
    }
    return count;
}

int util_path_make_relative(const char *base_path, char *target_path) {
    if(!base_path || !target_path) {
        return A2_ERR;
    }

    char base_buf[PATH_MAX];
    char target_buf[PATH_MAX];
    size_t base_len = strlen(base_path);
    size_t target_len = strlen(target_path);
    if(base_len == 0 || target_len == 0 || base_len >= PATH_MAX || target_len >= PATH_MAX) {
        return A2_ERR;
    }

    memcpy(base_buf, base_path, base_len + 1);
    memcpy(target_buf, target_path, target_len + 1);

    int base_root_len = util_path_root_len(base_buf);
    int target_root_len = util_path_root_len(target_buf);
    if(base_root_len == 0 || target_root_len == 0) {
        return A2_ERR;
    }
    if(base_root_len != target_root_len) {
        return A2_ERR;
    }
#ifdef _WIN32
    if(strnicmp(base_buf, target_buf, (size_t)base_root_len) != 0) {
        return A2_ERR;
    }
#else
    if(strncmp(base_buf, target_buf, (size_t)base_root_len) != 0) {
        return A2_ERR;
    }
#endif

    char *base_ptr = base_buf + base_root_len;
    char *target_ptr = target_buf + target_root_len;

    char *base_comps[PATH_MAX / 2];
    char *target_comps[PATH_MAX / 2];
    int base_count = util_path_split_components(base_ptr, base_comps, (int)(PATH_MAX / 2));
    int target_count = util_path_split_components(target_ptr, target_comps, (int)(PATH_MAX / 2));

    int common = 0;
    int max_common = (base_count < target_count) ? base_count : target_count;
    while(common < max_common) {
#ifdef _WIN32
        if(stricmp(base_comps[common], target_comps[common]) != 0) {
            break;
        }
#else
        if(strcmp(base_comps[common], target_comps[common]) != 0) {
            break;
        }
#endif
        common++;
    }

    int up_levels = base_count - common;

    char rel[PATH_MAX];
    size_t rel_len = 0;
    int remaining = target_count - common;

    if(up_levels == 0) {
        if(remaining <= 0) {
            rel[rel_len++] = '.';
        } else {
            rel[rel_len++] = '.';
            for(int i = common; i < target_count; i++) {
                size_t part_len = strlen(target_comps[i]);
                if(rel_len && rel_len + 1 < PATH_MAX) {
                    rel[rel_len++] = PATH_SEPARATOR;
                }
                if(rel_len + part_len >= PATH_MAX) {
                    return A2_ERR;
                }
                memcpy(rel + rel_len, target_comps[i], part_len);
                rel_len += part_len;
            }
        }
    } else {
        for(int up = 0; up < up_levels; up++) {
            if(rel_len + 2 >= PATH_MAX) {
                return A2_ERR;
            }
            if(rel_len) {
                rel[rel_len++] = PATH_SEPARATOR;
            }
            rel[rel_len++] = '.';
            rel[rel_len++] = '.';
        }
        if(remaining > 0) {
            if(rel_len + 1 >= PATH_MAX) {
                return A2_ERR;
            }
            rel[rel_len++] = PATH_SEPARATOR;
            for(int i = common; i < target_count; i++) {
                size_t part_len = strlen(target_comps[i]);
                if(i > common) {
                    if(rel_len + 1 >= PATH_MAX) {
                        return A2_ERR;
                    }
                    rel[rel_len++] = PATH_SEPARATOR;
                }
                if(rel_len + part_len >= PATH_MAX) {
                    return A2_ERR;
                }
                memcpy(rel + rel_len, target_comps[i], part_len);
                rel_len += part_len;
            }
        }
    }

    if(rel_len == 0 || rel_len >= PATH_MAX) {
        return A2_ERR;
    }
    rel[rel_len] = '\0';
    memcpy(target_path, rel, rel_len + 1);
    return A2_OK;
}

int util_path_resolve(const char *base_path, const char *path, char *out, size_t out_sz) {
    if(!path || !out || out_sz == 0) {
        return A2_ERR;
    }

    size_t path_len = strlen(path);
    if(path_len >= out_sz || path_len >= PATH_MAX) {
        return A2_ERR;
    }

    if(!base_path || !*base_path) {
        memcpy(out, path, path_len + 1);
        return A2_OK;
    }

    char path_buf[PATH_MAX];
    memcpy(path_buf, path, path_len + 1);
    if(util_path_root_len(path_buf) > 0) {
        memcpy(out, path, path_len + 1);
        return A2_OK;
    }

    size_t base_len = strlen(base_path);
    if(base_len == 0 || base_len >= PATH_MAX || base_len >= out_sz) {
        return A2_ERR;
    }

    char base_buf[PATH_MAX];
    memcpy(base_buf, base_path, base_len + 1);

    int base_root_len = util_path_root_len(base_buf);
    if(base_root_len == 0) {
        return A2_ERR;
    }

    char *base_ptr = base_buf + base_root_len;
    char *rel_ptr = path_buf;

    char *base_comps[PATH_MAX / 2];
    char *rel_comps[PATH_MAX / 2];
    char *stack[PATH_MAX / 2];
    int base_count = util_path_split_components(base_ptr, base_comps, (int)(PATH_MAX / 2));
    int rel_count = util_path_split_components(rel_ptr, rel_comps, (int)(PATH_MAX / 2));

    int stack_count = 0;
    for(int i = 0; i < base_count; i++) {
        stack[stack_count++] = base_comps[i];
    }

    for(int i = 0; i < rel_count; i++) {
        if(strcmp(rel_comps[i], ".") == 0) {
            continue;
        }
        if(strcmp(rel_comps[i], "..") == 0) {
            if(stack_count > 0) {
                stack_count--;
            }
            continue;
        }
        if(stack_count >= (int)(PATH_MAX / 2)) {
            return A2_ERR;
        }
        stack[stack_count++] = rel_comps[i];
    }

    size_t out_len = 0;
    if((size_t)base_root_len >= out_sz) {
        return A2_ERR;
    }
    memcpy(out, base_buf, (size_t)base_root_len);
    out_len = (size_t)base_root_len;
    if(out_len > 0 && !util_path_is_sep(out[out_len - 1])) {
        if(out_len + 1 >= out_sz) {
            return A2_ERR;
        }
        out[out_len++] = PATH_SEPARATOR;
    }

    for(int i = 0; i < stack_count; i++) {
        size_t part_len = strlen(stack[i]);
        if(out_len && !util_path_is_sep(out[out_len - 1])) {
            if(out_len + 1 >= out_sz) {
                return A2_ERR;
            }
            out[out_len++] = PATH_SEPARATOR;
        }
        if(out_len + part_len >= out_sz) {
            return A2_ERR;
        }
        memcpy(out + out_len, stack[i], part_len);
        out_len += part_len;
    }

    if(out_len == 0 || out_len >= out_sz) {
        return A2_ERR;
    }
    out[out_len] = '\0';
    return A2_OK;
}

void util_format_path(const char *base_path, const char *path, char *out, size_t out_sz) {
    if(!out || out_sz == 0) {
        return;
    }
    if(!path) {
        out[0] = '\0';
        return;
    }
    strncpy(out, path, out_sz - 1);
    out[out_sz - 1] = '\0';
    if(base_path && base_path[0]) {
        char rel[PATH_MAX];
        strncpy(rel, path, PATH_MAX - 1);
        rel[PATH_MAX - 1] = '\0';
        if(util_path_make_relative(base_path, rel) == A2_OK) {
            strncpy(out, rel, out_sz - 1);
            out[out_sz - 1] = '\0';
        }
    }
}

void *util_memset32(void *ptr, uint32_t value, size_t count) {
    uint32_t *p = ptr;
    while(count--) {
        *p++ = value;
    }
    return ptr;
}

char *util_strndup(const char *string, size_t length) {
    char *string_copy = (char *)malloc(length + 1);
    if(string_copy) {
        strncpy(string_copy, string, length);
        string_copy[length] = '\0';
    }
    return string_copy;
}

char util_character_in_characters(const char character, const char *characters) {
    while(*characters && character != *characters) {
        characters++;
    }
    return *characters;
}

int util_is_newline(char c) {
    return (c == '\n' || c == '\r');
}

char *util_extract_fqn(const char *string, int str_len, int *index) {
    if(*index >= str_len) {
        return NULL;
    }
    const char *s = &string[*index];
    char find;
    if(*s == '"') {
        find = '"';
        s++;
    } else {
        find = ',';
    }
    const char *e = s;
    while(*e && *e != find) {
        e++;
    }
    if(e - string > str_len) {
        return NULL;
    }
    int len = e - s;
    char *fn = (char *)malloc(len + 1);
    if(!fn) {
        return NULL;
    }
    // strncpy(fn, s, len);
    memcpy(fn, s, len);
    fn[len] = '\0';
    if(*e == find) {
        e++;
        while(*e == ' ' || *e == ',' && *e != '"') {
            e++;
        };
    }
    *index = e - string;
    return fn;
}

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1a_hash
uint32_t util_fnv_1a_hash(const char *key, size_t len) {
    uint32_t hash = 2166136261u;                            // FNV offset basis
    for(size_t i = 0; i < len; i++) {
        hash ^= (uint8_t) tolower(key[i]);
        hash *= 16777619;                                   // FNV prime
    }
    return hash;
}

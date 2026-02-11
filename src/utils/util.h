// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    char        name[PATH_MAX];     // UTF-8 filename (no path)
    int         name_length;
    size_t      size;               // bytes (0 for directories)
    uint64_t    mtime_sec;          // seconds since Unix epoch
    uint32_t    is_file: 1;         // 1 if regular file, 0 otherwise
    uint32_t    is_directory: 1;    // 1 if directory, 0 otherwise
} FILE_INFO;

typedef struct {
    FILE *fp;
    char *file_display_name;        // Points into file_path
    char *file_path;                // UTF-8 full path
    char *file_mode;                // How it was opened, example "rb+"
    char *file_data;                // Buffer with the contents of the file if it was loaded
    int64_t file_size;              // File's size
    size_t load_padding;            // When alloc'ing a buffer to load the file, add this to size
    uint8_t is_used: 1;
    uint8_t is_file_open: 1;
    uint8_t is_file_loaded: 1;
} UTIL_FILE;

typedef enum {
    CONSOLE_NONE,
    CONSOLE_EXISTING,
    CONSOLE_NEW
} CONSOLE_MODE;

// Prototype callback for ini file loading
typedef int (*INI_PAIR_CALLBACK)(void *user_data, const char *section, const char *key, const char *value);

int util_console_open(CONSOLE_MODE *console_mode);
void util_console_close(CONSOLE_MODE console_mode);

int util_dir_change(const char *path);
int util_dir_get_current(char *buffer, size_t buffer_size);
int util_dir_load_contents(DYNARRAY *array);

void util_file_close(UTIL_FILE *f);
void util_file_discard(UTIL_FILE *f);
int util_file_load(UTIL_FILE *f, const char *file_name, const char *file_mode);
int util_file_open(UTIL_FILE *f, const char *file_name, const char *file_mode);
int util_file_stat_regular_utf8(const char *path, uint64_t *size_out, uint64_t *mtime_out);

char *util_ini_find_character(char **start, char character);
char *util_ini_get_line(char **start, char *end);
char *util_ini_next_token(char **start);
int util_ini_load_file(const char *filename, INI_PAIR_CALLBACK callback, void *user_data);
int util_ini_save_file(const char *filename, INI_STORE *ini_store);

const char *util_strinstr(const char *haystack, const char *needle, int needle_length);
const char *util_strrtok(const char *str, const char *delim);
char *util_strtok_r(char *s, const char *delim, char **ctx);
int util_path_needs_quotes(const char *path);
int util_path_make_relative(const char *base_path, char *target_path);
int util_path_resolve(const char *base_path, const char *path, char *out, size_t out_sz);
void util_format_path(const char *base_path, const char *path, char *out, size_t out_sz);
int util_file_info_qsort_cmp(const void *p1, const void *p2);
void *util_memset32(void *ptr, uint32_t value, size_t count);
char *util_strndup(const char *string, size_t length);
char util_character_in_characters(const char character, const char *characters);
int util_is_newline(char c);
char *util_extract_fqn(const char *string, int str_len, int *index);

uint32_t util_fnv_1a_hash(const char *key, size_t len);

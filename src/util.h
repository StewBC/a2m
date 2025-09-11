// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

typedef struct {
    char     name[PATH_MAX];    // UTF-8 filename (no path)
    int      name_length;
    size_t   size;              // bytes (0 for directories)
    uint64_t mtime_sec;         // seconds since Unix epoch
    int      is_file:1;         // 1 if regular file, 0 otherwise
    int      is_directory:1;    // 1 if directory, 0 otherwise
} FILE_INFO;

typedef struct UTIL_FILE {
    FILE *fp;
    char *file_display_name;    // Points into file_path
    char *file_path;            // UTF-8 full path
    char *file_mode;            // How it was opened, example "rb+"
    char *file_data;            // Buffer with the contents of the file if it was loaded
    int64_t file_size;          // File's size
    size_t load_padding;        // When alloc'ing a buffer to load the file, add this to size
    uint8_t is_used:1;
    uint8_t is_file_open:1;
    uint8_t is_file_loaded:1;
} UTIL_FILE;

// Prototype callback for ini file loading
typedef void (*INI_PAIR_CALLBACK)(void *user_data, char *section, char *key, char *value);

int util_dir_change(const char *path);
int util_dir_get_current(char *buffer, size_t buffer_size);
int util_dir_load_contents(DYNARRAY * array);

void util_file_close(UTIL_FILE * f);
void util_file_discard(UTIL_FILE * f);
int util_file_load(UTIL_FILE * f, const char *file_name, const char *file_mode);
int util_file_open(UTIL_FILE * f, const char *file_name, const char *file_mode);
int file_stat_regular_utf8(const char *path, uint64_t *size_out, uint64_t *mtime_out);

char *util_ini_find_character(char **start, char character);
char *util_ini_get_line(char **start, char *end);
char *util_ini_next_token(char **start);
int util_ini_load_file(char *filename, INI_PAIR_CALLBACK callback, void *user_data);

const char *util_strinstr(const char *haystack, const char *needle, int needle_length);
char *util_strrtok(char *str, const char *delim);
int util_qsort_cmp(const void *p1, const void *p2);

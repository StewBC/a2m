#pragma once

#include "a2_status.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *fp;
    char *file_display_name;
    char *file_path;
    char *file_mode;
    char *file_data;
    int64_t file_size;
    size_t load_padding;
    uint8_t is_used: 1;
    uint8_t is_file_open: 1;
    uint8_t is_file_loaded: 1;
} UTIL_FILE;

void util_file_close(UTIL_FILE *f);
void util_file_discard(UTIL_FILE *f);
int util_file_load(UTIL_FILE *f, const char *file_name, const char *file_mode);
int util_file_open(UTIL_FILE *f, const char *file_name, const char *file_mode);

#include "symbol_table.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum {
    SYMBOL_FILE_LINE_MAX = 1024,
    SYMBOL_FILE_LABEL_MAX = 128
};

static int symbol_file_hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    ch = tolower((unsigned char)ch);
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    return -1;
}

static bool symbol_file_parse_address_token(const char *text, uint16_t *out_address, const char **out_after)
{
    const char *cursor = text;
    unsigned value = 0;
    int digits = 0;

    if (cursor[0] == '$') {
        cursor++;
    } else if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }

    while (digits < 4) {
        int digit = symbol_file_hex_value((unsigned char)*cursor);
        if (digit < 0) {
            break;
        }
        value = (value << 4) | (unsigned)digit;
        cursor++;
        digits++;
    }

    if (digits != 4 || symbol_file_hex_value((unsigned char)*cursor) >= 0) {
        return false;
    }
    if (*cursor != '\0' && !isspace((unsigned char)*cursor)) {
        return false;
    }

    *out_address = (uint16_t)value;
    *out_after = cursor;
    return true;
}

static bool symbol_file_parse_line(const char *line, uint16_t *out_address, char *out_label, size_t out_label_size)
{
    const char *cursor;

    if (line == NULL || out_address == NULL || out_label == NULL || out_label_size == 0) {
        return false;
    }

    out_label[0] = '\0';
    for (cursor = line; *cursor != '\0'; ++cursor) {
        const char *after_address;
        const char *label_start;
        size_t label_length;

        if (cursor != line && !isspace((unsigned char)cursor[-1])) {
            continue;
        }
        if (!symbol_file_parse_address_token(cursor, out_address, &after_address)) {
            continue;
        }

        label_start = after_address;
        while (isspace((unsigned char)*label_start)) {
            label_start++;
        }
        if (*label_start == '\0') {
            continue;
        }

        label_length = 0;
        while (label_start[label_length] != '\0' &&
               !isspace((unsigned char)label_start[label_length])) {
            label_length++;
        }
        if (label_length == 0 || label_length >= out_label_size) {
            continue;
        }

        memcpy(out_label, label_start, label_length);
        out_label[label_length] = '\0';
        return true;
    }

    return false;
}

symbol_result symbol_table_load_file(
    symbol_table *table,
    const char *path,
    const char *source_name,
    size_t *out_loaded)
{
    FILE *file;
    char line[SYMBOL_FILE_LINE_MAX];
    size_t loaded = 0;

    if (out_loaded != NULL) {
        *out_loaded = 0;
    }
    if (table == NULL || path == NULL || path[0] == '\0') {
        return SYMBOL_INVALID;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return SYMBOL_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        uint16_t address;
        char label[SYMBOL_FILE_LABEL_MAX];

        if (!symbol_file_parse_line(line, &address, label, sizeof(label))) {
            continue;
        }
        if (symbol_table_add(
                table,
                address,
                label,
                SYMBOL_SOURCE_FILE,
                source_name != NULL && source_name[0] != '\0' ? source_name : path,
                false) ==
            SYMBOL_OUT_OF_MEMORY) {
            fclose(file);
            return SYMBOL_OUT_OF_MEMORY;
        }
        loaded++;
    }

    fclose(file);
    if (out_loaded != NULL) {
        *out_loaded = loaded;
    }
    return SYMBOL_OK;
}

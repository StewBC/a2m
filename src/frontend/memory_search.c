#include "memory_search.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void memory_search_set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u) {
        snprintf(error, error_size, "%s", message);
    }
}

static int memory_search_hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return 10 + (int)(c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (int)(c - 'A');
    return -1;
}

bool memory_search_parse(
    const char *query,
    memory_search_mode mode,
    bool ignore_case,
    memory_search_pattern *out,
    char *error,
    size_t error_size)
{
    size_t length;

    if (error != NULL && error_size > 0u) error[0] = '\0';
    if (query == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));

    if (mode == MEMORY_SEARCH_STRING) {
        length = strlen(query);
        if (length == 0u) {
            memory_search_set_error(error, error_size, "Enter a search string");
            return false;
        }
        if (length > MEMORY_SEARCH_PATTERN_MAX) {
            memory_search_set_error(error, error_size, "Search string is too long");
            return false;
        }
        memcpy(out->bytes, query, length);
        out->length = length;
        out->ignore_case = ignore_case;
        if (ignore_case) {
            size_t i;
            for (i = 0; i < length; ++i) {
                out->bytes[i] = (uint8_t)tolower((unsigned char)out->bytes[i]);
            }
        }
        return true;
    }

    {
        int high = -1;
        const unsigned char *p = (const unsigned char *)query;
        while (*p != '\0') {
            int digit;
            if (isspace(*p)) {
                ++p;
                continue;
            }
            digit = memory_search_hex_digit(*p++);
            if (digit < 0) {
                memory_search_set_error(error, error_size, "Hex search accepts only hex digits and spaces");
                return false;
            }
            if (high < 0) {
                high = digit;
            } else {
                if (out->length >= MEMORY_SEARCH_PATTERN_MAX) {
                    memory_search_set_error(error, error_size, "Hex pattern is too long");
                    return false;
                }
                out->bytes[out->length++] = (uint8_t)((high << 4) | digit);
                high = -1;
            }
        }
        if (high >= 0) {
            memory_search_set_error(error, error_size, "Hex search needs complete byte pairs");
            return false;
        }
        if (out->length == 0u) {
            memory_search_set_error(error, error_size, "Enter a hex pattern");
            return false;
        }
    }
    return true;
}

bool memory_search_find(
    const uint8_t *bytes,
    const uint8_t *valid,
    const memory_search_pattern *pattern,
    uint16_t start_address,
    bool reverse,
    uint16_t *out_address)
{
    uint32_t distance;

    if (bytes == NULL || pattern == NULL || pattern->length == 0u || out_address == NULL) {
        return false;
    }

    for (distance = 1u; distance <= 65536u; ++distance) {
        uint16_t candidate = reverse ?
            (uint16_t)(start_address - distance) :
            (uint16_t)(start_address + distance);
        size_t i;
        bool matches = true;

        for (i = 0u; i < pattern->length; ++i) {
            uint16_t address = (uint16_t)(candidate + (uint16_t)i);
            uint8_t value;
            if (valid != NULL && valid[address] == 0u) {
                matches = false;
                break;
            }
            value = bytes[address];
            if (pattern->ignore_case) {
                value = (uint8_t)tolower((unsigned char)value);
            }
            if (value != pattern->bytes[i]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            *out_address = candidate;
            return true;
        }
    }
    return false;
}

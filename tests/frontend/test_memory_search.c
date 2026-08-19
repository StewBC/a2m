#include "memory_search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(const char *name, int value)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void expect_address(const char *name, uint16_t expected, uint16_t actual)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %04X, got %04X\n", name, expected, actual);
        exit(1);
    }
}

int main(void)
{
    uint8_t bytes[65536];
    uint8_t valid[65536];
    memory_search_pattern pattern;
    char error[96];
    uint16_t found;

    memset(bytes, 0, sizeof(bytes));
    memset(valid, 1, sizeof(valid));

    expect_true("parse string",
        memory_search_parse("Apple", MEMORY_SEARCH_STRING, false, &pattern, error, sizeof(error)));
    memcpy(bytes + 0x1234, "Apple", 5);
    memcpy(bytes + 0x4321, "Apple", 5);
    expect_true("find forward",
        memory_search_find(bytes, NULL, &pattern, 0x1000, false, &found));
    expect_address("forward address", 0x1234, found);
    expect_true("find next",
        memory_search_find(bytes, NULL, &pattern, found, false, &found));
    expect_address("next address", 0x4321, found);
    expect_true("find previous",
        memory_search_find(bytes, NULL, &pattern, found, true, &found));
    expect_address("previous address", 0x1234, found);

    expect_true("parse insensitive string",
        memory_search_parse("apple", MEMORY_SEARCH_STRING, true, &pattern, error, sizeof(error)));
    expect_true("find insensitive",
        memory_search_find(bytes, NULL, &pattern, 0x1000, false, &found));
    expect_address("insensitive address", 0x1234, found);

    expect_true("parse spaced hex",
        memory_search_parse("DE AD be EF", MEMORY_SEARCH_HEX, false, &pattern, error, sizeof(error)));
    bytes[0xFFFE] = 0xDE;
    bytes[0xFFFF] = 0xAD;
    bytes[0x0000] = 0xBE;
    bytes[0x0001] = 0xEF;
    expect_true("find wrapping hex",
        memory_search_find(bytes, NULL, &pattern, 0xF000, false, &found));
    expect_address("wrapping address", 0xFFFE, found);

    expect_true("reject odd hex",
        !memory_search_parse("ABC", MEMORY_SEARCH_HEX, false, &pattern, error, sizeof(error)));
    expect_true("odd hex error", strstr(error, "byte pairs") != NULL);

    expect_true("parse validity pattern",
        memory_search_parse("Apple", MEMORY_SEARCH_STRING, false, &pattern, error, sizeof(error)));
    valid[0x1236] = 0;
    expect_true("skip invalid match",
        memory_search_find(bytes, valid, &pattern, 0x1000, false, &found));
    expect_address("valid match address", 0x4321, found);

    printf("ok memory search\n");
    return 0;
}

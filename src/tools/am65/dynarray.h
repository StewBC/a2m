// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t items;
    size_t element_size;
} AM65_DYNARRAY;

int am65_array_add(AM65_DYNARRAY *array, void *element);
int am65_array_copy_items(AM65_DYNARRAY *array, size_t start_index, size_t end_index, size_t to_index);
void am65_array_free(AM65_DYNARRAY *array);
void *am65_array_get(const AM65_DYNARRAY *array, size_t index);
void am65_array_init(AM65_DYNARRAY *array, size_t element_size);
int am65_array_remove(AM65_DYNARRAY *array, void *element);
int am65_array_resize(AM65_DYNARRAY *array, size_t new_size);

#define AM65_ARRAY_ADD(array, value) am65_array_add((array), &(value))
#define AM65_ARRAY_GET(array, type, index) ((type *)am65_array_get((array), (index)))
#define AM65_ARRAY_INIT(array, type) am65_array_init((array), sizeof(type))

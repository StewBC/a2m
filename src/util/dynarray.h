#pragma once

#include <stddef.h>

typedef struct {
    void *data;
    size_t size;
    size_t items;
    size_t element_size;
} DYNARRAY;

int array_add(DYNARRAY *array, void *element);
int array_copy_items(DYNARRAY *array, size_t start_index, size_t end_index, size_t to_index);
void array_free(DYNARRAY *array);
void *array_get(const DYNARRAY *array, size_t index);
void array_init(DYNARRAY *array, size_t element_size);
int array_remove(DYNARRAY *array, void *element);
int array_resize(DYNARRAY *array, size_t new_size);

#define ARRAY_ADD(array, value) array_add((array), &(value))
#define ARRAY_GET(array, type, index) ((type *)array_get((array), (index)))
#define ARRAY_INIT(array, type) array_init((array), sizeof(type))

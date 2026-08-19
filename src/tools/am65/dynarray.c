// 6502 assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "asm_lib.h"

int am65_array_add(AM65_DYNARRAY *array, void *element) {
    if(array->items == array->size) {
        if(ASM_OK != am65_array_resize(array, array->size == 0 ? 1 : array->size * 2)) {
            return ASM_ERR;
        }
    }
    if(element) {
        memcpy((char *)array->data + array->items * array->element_size, element, array->element_size);
    }
    array->items++;
    return ASM_OK;
}

int am65_array_copy_items(AM65_DYNARRAY *array, size_t start_index, size_t end_index, size_t to_index) {
    size_t num_items = end_index - start_index;
    size_t next_index = to_index + num_items;
    if(next_index >= array->size) {
        if(ASM_OK != am65_array_resize(array, next_index)) {
            return ASM_ERR;
        }
    }

    memmove((char *)array->data + to_index * array->element_size,
            (char *)array->data + start_index * array->element_size,
            num_items * array->element_size);
    if(next_index > array->items) {
        array->items = next_index;
    }
    return ASM_OK;
}

void am65_array_free(AM65_DYNARRAY *array) {
    free(array->data);
    array->data = NULL;
    array->size = 0;
    array->items = 0;
}

void *am65_array_get(const AM65_DYNARRAY *array, size_t index) {
    if(index < array->size) {
        return (char *)array->data + index * array->element_size;
    }
    return NULL;
}

void am65_array_init(AM65_DYNARRAY *array, size_t element_size) {
    array->data = NULL;
    array->size = 0;
    array->items = 0;
    array->element_size = element_size;
}

int am65_array_remove(AM65_DYNARRAY *array, void *element) {
    uint8_t *end = (uint8_t *)array->data + array->element_size * array->items;
    if(array->data <= element && (void *)end > element) {
        uint8_t *start = (uint8_t *)element + array->element_size;
        memmove(element, start, end - start);
        array->items--;
        return ASM_OK;
    }
    return ASM_ERR;
}

int am65_array_resize(AM65_DYNARRAY *array, size_t new_size) {
    void *temp = realloc(array->data, new_size * array->element_size);
    if(temp == NULL) {
        return ASM_ERR;
    }
    array->data = temp;
    array->size = new_size;
    return ASM_OK;
}

// Apple ][+ and //e Emhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "utils_lib.h"

// Add an element to the array.  If element is NULL then add space for an element
int array_add(DYNARRAY *array, void *element) {
    // If array is full, attempt to resize
    if(array->items == array->size) {
        if(A2_OK != array_resize(array, array->size == 0 ? 1 : array->size * 2)) {
            return A2_ERR;                                  // Resize failed, return error
        }
    }
    // Add element to the array
    if(element) {
        memcpy((char *) array->data + (array->items * array->element_size), element, array->element_size);
    }
    array->items++;
    return A2_OK;
}

// Copy element in array from old to new index
int array_copy_items(DYNARRAY *array, size_t start_index, size_t end_index, size_t to_index) {
    size_t num_items = end_index - start_index;
    size_t next_index = to_index + num_items;
    if(next_index >= array->size) {
        if(A2_OK != array_resize(array, next_index)) {
            return A2_ERR;
        }
    }

    memmove((char *) array->data + to_index * array->element_size, (char *) array->data + start_index * array->element_size,
            num_items * array->element_size);
    if(next_index > array->items) {
        array->items = next_index;
    }
    return A2_OK;
}

// Free the array
void array_free(DYNARRAY *array) {
    free(array->data);
    array->data = NULL;
    array->size = 0;
    array->items = 0;
}

// Get element at index
void *array_get(DYNARRAY *array, size_t index) {
    if(index < array->size) {
        return (char *) array->data + (index * array->element_size);
    }
    return NULL;                                            // Out of bounds
}

// Initialize the array
void array_init(DYNARRAY *array, size_t element_size) {
    array->data = NULL;
    array->size = 0;
    array->items = 0;
    array->element_size = element_size;
}

// Remove the element from the array, moving all higher elements down
int array_remove(DYNARRAY *array, void *element) {
    uint8_t *end = (uint8_t *) array->data + array->element_size * array->items;
    if(array->data <= element && (void *) end > element) {
        uint8_t *start = (uint8_t *) element + array->element_size;
        memmove(element, start, end - start);
        array->items--;
    }
    return A2_ERR;
}

// Resize the array
int array_resize(DYNARRAY *array, size_t new_size) {
    // Attempt to reallocate with a temporary pointer
    void *temp = realloc(array->data, new_size * array->element_size);

    // Check if reallocation was successful
    if(temp == NULL) {
        return A2_ERR;                                      // Memory allocation failed
    }
    // Reallocation successful; update array->data
    array->data = temp;
    array->size = new_size;
    return A2_OK;
}

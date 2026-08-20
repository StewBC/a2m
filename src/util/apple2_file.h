#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum apple2_binary_format {
    APPLE2_BINARY_FORMAT_AUTO = 0,
    APPLE2_BINARY_FORMAT_RAW,
    APPLE2_BINARY_FORMAT_NAPS,
    APPLE2_BINARY_FORMAT_APPLESINGLE,
    APPLE2_BINARY_FORMAT_LEGACY_DOS
} apple2_binary_format;

typedef struct apple2_binary_view {
    const uint8_t *data;
    size_t size;
    uint16_t load_address;
    bool has_load_address;
    uint8_t prodos_type; /* set when has_prodos_type; $06 BIN or $FF SYS */
    bool has_prodos_type;
    apple2_binary_format format;
} apple2_binary_view;

/* ProDOS types that carry a load address in aux and are safe to inject/run. */
bool apple2_binary_prodos_type_is_loadable(uint16_t prodos_type);

bool apple2_naps_parse_path(const char *path, uint8_t *file_type, uint16_t *aux_type);
bool apple2_naps_make_path(
    const char *path,
    uint8_t file_type,
    uint16_t aux_type,
    char *out,
    size_t out_size);

bool apple2_binary_decode(
    const char *path,
    const uint8_t *bytes,
    size_t size,
    apple2_binary_format requested,
    uint16_t raw_address,
    apple2_binary_view *out,
    char *error,
    size_t error_size);

bool apple2_applesingle_encode_bin(
    const uint8_t *bytes,
    size_t size,
    uint16_t load_address,
    uint8_t **out_bytes,
    size_t *out_size);

bool apple2_applesoft_tokenize(
    const uint8_t *text,
    size_t text_size,
    uint8_t **out_program,
    size_t *out_size,
    char *error,
    size_t error_size);

bool apple2_applesoft_detokenize(
    const uint8_t *program,
    size_t program_size,
    uint8_t **out_text,
    size_t *out_size,
    char *error,
    size_t error_size);

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Forward
typedef struct DISKII_DRIVE DISKII_DRIVE;

// #define DISKII_HALFTRACKS   70
#define DISKII_QUATERTRACKS 140            // 35 tracks, 70 half tracks 140 quater tracks
// #define DISKII_MAX_SECTORS  16
// #define WOZ_QTRACKS         160            // quarter-track map entries in WOZ (v1/v2 5.25")

typedef enum {
    DSK_SECTOR_ORDER_DOS33,            // physical 16-sector interleave
    DSK_SECTOR_ORDER_PRODOS,           // logical 0..15
    DSK_SECTOR_ORDER_OTHER
} DISKII_SECTOR_ORDER;

typedef enum {
    DSK_ENCODING_13SECTOR,             // DOS 3.2
    DSK_ENCODING_16SECTOR              // DOS 3.3 / ProDOS
} DISKII_ENCODING;

// Emulator support for differenty image types
typedef enum {
    IMG_DSK,
    IMG_WOZ,
    IMG_NIB
} DISKII_KIND;

// NIB file specific data
typedef struct IMAGE_NIB {
    uint32_t writable;        // 0 = no
    uint32_t track_size;      // 6656 or 6384 bytes
    uint32_t num_tracks;      // Typically 35
    uint32_t track_index_pos; // byte offset into file for current track start
    uint32_t track_read_pos;  // "head" offset from track start to byte
} IMAGE_NIB;

// The file that's loaded
typedef struct DISKII_IMAGE {
    UTIL_FILE file;
    DISKII_KIND kind;
    DISKII_ENCODING disk_encoding;
    void *image_specifics;    // kind's data (image_dsk_t, IMAGE_NIB, image_woz_t)
} DISKII_IMAGE;

uint8_t image_get_byte(APPLE2 *m, DISKII_DRIVE *d);
void image_head_position(DISKII_IMAGE *image, uint32_t quater_track);
int image_load_dsk(APPLE2 *m, DISKII_IMAGE *image, const char *ext);
int image_load_nib(APPLE2 *m, DISKII_IMAGE *image);
int image_load_woz(APPLE2 *m, DISKII_IMAGE *image);
void image_shutdown(DISKII_IMAGE *image);

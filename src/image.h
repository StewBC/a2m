// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// Forward
typedef struct _diskii_drive diskii_drive_t;

// #define DISKII_HALFTRACKS   70
#define DISKII_QUATERTRACKS 140            // 35 tracks, 70 half tracks 140 quater tracks
// #define DISKII_MAX_SECTORS  16
// #define WOZ_QTRACKS         160            // quarter-track map entries in WOZ (v1/v2 5.25")

typedef enum {
    DSK_SECTOR_ORDER_DOS33,            // physical 16-sector interleave
    DSK_SECTOR_ORDER_PRODOS,           // logical 0..15
    DSK_SECTOR_ORDER_OTHER
} disk_sector_order_t;

typedef enum {
    DSK_ENCODING_13SECTOR,             // DOS 3.2
    DSK_ENCODING_16SECTOR              // DOS 3.3 / ProDOS
} disk_encoding_t;

// Emulator support for differenty image types
typedef enum {
    IMG_DSK,
    IMG_WOZ,
    IMG_NIB
} diskii_kind_t;

// NIB file specific data
typedef struct _image_nib {
    uint32_t writable;        // 0 = no
    uint32_t track_size;      // 6656 or 6384 bytes
    uint32_t num_tracks;      // Typically 35
    uint32_t track_index_pos; // byte offset into file for current track start
    uint32_t track_read_pos;  // "head" offset from track start to byte
} image_nib_t;

// The file that's loaded
typedef struct _diskii_image {
    UTIL_FILE file;
    diskii_kind_t kind;
    disk_encoding_t disk_encoding;
    void *image_specifics;    // kind's data (image_dsk_t, image_nib_t, image_woz_t)
} diskii_image_t;

uint8_t image_get_byte(APPLE2 *m, diskii_drive_t *d);
void image_head_position(diskii_image_t *image, uint32_t quater_track);
int image_load_dsk(APPLE2 *m, diskii_image_t *image, const char *ext);
int image_load_nib(APPLE2 *m, diskii_image_t *image);
int image_load_woz(APPLE2 *m, diskii_image_t *image);
void image_shutdown(diskii_image_t *image);

// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

// SmartPort Block Device
typedef struct SP_DEVICE {
    uint8_t     sp_active;
    uint8_t     sp_status;
    size_t      sp_read_offset;
    size_t      sp_write_offset;
    uint8_t     sp_buffer[512+4];
    UTIL_FILE   sp_files[2];
    size_t      file_header_size[2];
} SP_DEVICE;
#define SP_BLOCK_SIZE       512
#define SP_SUCCESS          0x00
#define SP_IO_ERROR         0x27
#define SP_WRITE_PROTECT    0x2B

int sp_mount(APPLE2 *m, const int slot, const int device, const char *file_name);
void sp_status(APPLE2 *m, const int slot);
void sp_read(APPLE2 *m, const int slot);
void sp_write(APPLE2 *m, const int slot);

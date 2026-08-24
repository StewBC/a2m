// Apple ][+ and //e Enhanced emulator
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "hostfs.h"
#include "util_file.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct apple2;

typedef enum {
    SP_BACKEND_NONE = 0,
    SP_BACKEND_IMAGE = 1,
    SP_BACKEND_HOSTFS = 2
} sp_backend_kind;

typedef struct {
    uint8_t sp_status;
    size_t sp_read_offset;
    size_t sp_write_offset;
    uint8_t sp_buffer[512 + 4];
    UTIL_FILE sp_files[2];
    size_t file_header_size[2];
    sp_backend_kind backend[2];
    hostfs_volume *hostfs[2];
} SP_DEVICE;

#define SP_BLOCK_SIZE       512
#define SP_SUCCESS          0x00
#define SP_IO_ERROR         0x27
#define SP_WRITE_PROTECT    0x2B
/* Device-select offsets within $C0s0..$C0sF (match a2m / SP firmware).
   Firmware for slot 7 uses $C0F4 (data) and $C0F5 (status handshake). */
#define SP_DATA             0x4
#define SP_STATUS           0x5

int sp_mount(struct apple2 *m, int slot, int device, const char *file_name);
int sp_eject(struct apple2 *m, int slot, int device);
int sp_flush_all(struct apple2 *m);
void sp_status(struct apple2 *m, int slot);
void sp_read(struct apple2 *m, int slot);
void sp_write(struct apple2 *m, int slot);
void sp_shutdown(struct apple2 *m);

/* True if the unit has image or HostFS media attached. */
bool sp_unit_mounted(const SP_DEVICE *spd, int device);
const char *sp_unit_path(const SP_DEVICE *spd, int device);
const char *sp_unit_display_name(const SP_DEVICE *spd, int device);

/*
 * Pure SmartPort entry host trap (no $C800 card firmware in tree).
 * When a SmartPort card owns $C800 (I/O SELECT latch) and 80-col firmware
 * is not overlaid (no CXROM / no $C3xx overlay), PC at $C800 / $C89B /
 * $C9AA is the missing expansion ROM: parse inline cmd + param list and
 * dispatch STATUS / READ_BLOCK / WRITE_BLOCK.
 * Returns true if the trap handled the call (PC/SP/flags updated).
 */
bool sp_host_trap(struct apple2 *m);

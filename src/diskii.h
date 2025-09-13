// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#pragma once

#include "image.h"

// A disk drive has attached media, a motor, head, etc.
typedef struct _diskii_drive {
    // Spindle Motor
    uint64_t motor_event_cycles;               // when motor was turned on/off
    double motor_rpm;                          // 0..300
    uint8_t motor_on;                          // 1 = spindle motor energised; 0 = not

    // Head
    uint64_t head_event_cycles;                // when head was moved
    uint8_t phase_mask;                        // bit on = phase on - active
    uint8_t last_on_phase_mask;                // bit on = phase on - for direction tracking
    int16_t quarter_track_pos;                 // quarter-track units (0..139 = 35 tracks)
    uint64_t head_settle_cycles;               // ~3ms per seek

    // selection & lines
    uint8_t q6;
    uint8_t q7;
    uint8_t sensor_protect;                    // 0 = enabled / 1 = write protected (notch)
    uint64_t q6_last_read_cycles;

    // Media
    diskii_image_t image;                      // mounted disk image
} diskii_drive_t;

// What the Apple II "talks to".  Each has 2 drives in this emulator
typedef struct DISKII_CONTROLLER {
    diskii_drive_t diskii_drive[2];
    uint64_t cycles_at_update;
    uint8_t active;                            // 0 or 1 for which drive is active
} DISKII_CONTROLLER;

void diskii_drive_select(APPLE2 *m, const int slot, int soft_switch);
void diskii_motor(APPLE2 *m, const int slot, int soft_switch);
int diskii_mount(APPLE2 *m, const int slot, const int device, const char *file_name);
uint8_t diskii_q6_access(APPLE2 *m, int slot, uint8_t on_off);
uint8_t diskii_q7_access(APPLE2 *m, int slot, uint8_t on_off);
void diskii_reset(APPLE2 *m);
void diskii_shutdown(APPLE2 *m);
void diskii_step_head(APPLE2 *m, const int slot, int soft_switch);

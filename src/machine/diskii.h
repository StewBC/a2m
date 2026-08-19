
#pragma once

#include "image.h"
#include "dynarray.h"

#include <stdint.h>

struct apple2;
typedef struct apple2 apple2_t;

/* Soft-switch nibble offsets at $C0sX (s = slot). */
enum {
    IWM_PH0_OFF     = 0x0,
    IWM_PH0_ON      = 0x1,
    IWM_PH1_OFF     = 0x2,
    IWM_PH1_ON      = 0x3,
    IWM_PH2_OFF     = 0x4,
    IWM_PH2_ON      = 0x5,
    IWM_PH3_OFF     = 0x6,
    IWM_PH3_ON      = 0x7,
    IWM_MOTOR_OFF   = 0x8,
    IWM_MOTOR_ON    = 0x9,
    IWM_SEL_DRIVE_1 = 0xA,
    IWM_SEL_DRIVE_2 = 0xB,
    IWM_Q6_OFF      = 0xC,
    IWM_Q6_ON       = 0xD,
    IWM_Q7_OFF      = 0xE,
    IWM_Q7_ON       = 0xF
};

// A disk drive has attached media, a motor, head, etc.
typedef struct DISKII_DRIVE {
    // Spindle Motor
    uint64_t motor_event_cycles;                // when motor was turned on/off
    uint64_t motor_off_delay_cycles;            // delay before motor-off starts spinning down
    double motor_rpm;                           // 0..300
    uint8_t motor_on;                           // 1 = spindle motor energised; 0 = not

    // Head
    uint64_t head_event_cycles;                 // when head was moved
    uint8_t phase_mask;                         // bit on = phase on - active
    uint8_t last_on_phase_mask;                 // bit on = phase on - for direction tracking
    int16_t quarter_track_pos;                  // quarter-track units (0..139 = 35 tracks)
    uint64_t head_settle_cycles;                // ~3ms per seek

    // selection & lines
    uint8_t q6;
    uint8_t q7;
    uint8_t sensor_protect;                     // 0 = enabled / 1 = write protected (notch)
    double q6_last_read_cycles;
    uint8_t read_latch;
    uint8_t write_latch;
    uint8_t write_latch_valid;
    uint8_t write_active;
    uint32_t write_track;
    uint32_t write_start_pos;
    uint32_t write_byte_count;

    // Media
    // DISKII_IMAGE image;                    // mounted disk image
    DYNARRAY images;                            // queue of disk imag
    DISKII_IMAGE *active_image;               // mounted disk image
    int image_index;                            // index if mounted image in queue
} DISKII_DRIVE;

// What the Apple II "talks to".  Each has 2 drives in this emulator
typedef struct {
    DISKII_DRIVE diskii_drive[2];
    uint64_t cycles_at_update;
    uint8_t active;                            // 0 or 1 for which drive is active
} DISKII_CONTROLLER;

void diskii_controller_init(DISKII_CONTROLLER *controller);
void diskii_drive_select(apple2_t *m, const int slot, int soft_switch);
void diskii_motor(apple2_t *m, const int slot, int soft_switch);
int diskii_eject(apple2_t *m, const int slot, const int device, int mount_next);
int diskii_mount(apple2_t *m, const int slot, const int device, const char *file_name);
uint8_t diskii_mount_image(apple2_t *m, const int slot, const int device, const int index);
/* Step multi-image queue: relative ±N, or absolute 1-based N. param 0 → next. */
int diskii_swap_image(
    apple2_t *m,
    const int slot,
    const int device,
    int32_t param,
    int relative);
int diskii_save(apple2_t *m, const int slot, const int device);
int diskii_flush_all(apple2_t *m);
uint8_t diskii_q6_access(apple2_t *m, int slot, uint8_t on_off, int write_access);
uint8_t diskii_q7_access(apple2_t *m, int slot, uint8_t on_off);
uint8_t diskii_latch_read(apple2_t *m, int slot);
void diskii_write_access(apple2_t *m, int slot, uint8_t value);
void diskii_reset(apple2_t *m);
void diskii_shutdown(apple2_t *m);
void diskii_step_head(apple2_t *m, const int slot, int soft_switch);

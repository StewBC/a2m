// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define ms_to_cycles(x) (x*(CPU_FREQUENCY/1000.0))
#define us_to_cycles(x) (x*(CPU_FREQUENCY/1000000.0))
#define count_phases(x) ((x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1) + ((x >> 3) & 1))
#define adj_pair(x)     (x == 0b0011 || x == 0b0110 || x == 0b1100 || x == 0b1001)
#define ROL4(x)         (((x << 1) | (x >> 3)) & 0xF)
#define ROR4(x)         (((x >> 1) | (x << 3)) & 0xF)

static inline void diskii_timer_update(uint64_t now, uint64_t *anchor, uint64_t *timer) {
    uint64_t delta = now - *anchor;
    *anchor = now;
    *timer = *timer > delta ? *timer - delta : 0;
}

void diskii_reset(APPLE2 *m) {
    for(int slot = 2; slot <= 7; slot++) {
        if(m->slot_cards[slot].slot_type == SLOT_TYPE_DISKII) {
            diskii_drive_select(m, slot, IWM_SEL_DRIVE_2);
            diskii_motor(m, slot, IWM_MOTOR_OFF);
            diskii_drive_select(m, slot, IWM_SEL_DRIVE_1);
            diskii_motor(m, slot, IWM_MOTOR_OFF);
        }
    }
}

void diskii_shutdown(APPLE2 *m) {
    for(int slot = 2; slot <= 7; slot++) {
        if(m->slot_cards[slot].slot_type == SLOT_TYPE_DISKII) {
        // SQW Clean out any allocated data for images and files and tracks and whatnot
        }
    }
}

int diskii_mount(APPLE2 *m, const int slot, const int device, const char *file_name) {
    DISKII_CONTROLLER *diic = &m->diskii_controller[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_DISKII) {
        return A2_ERR;
    }

    diskii_image_t *image = &diic->diskii_drive[device].image;
    UTIL_FILE *file = &image->file;
    if(A2_OK != util_file_load(file, file_name, "rb+")) {
        return A2_ERR;
    }

    const char *ext = strrchr(file_name, '.');
    // SQW - Write a better image type detect
    if(file->file_size == 143360 || (ext && 0 == stricmp(ext, ".dsk"))) {
        // DSK file
        return image_load_dsk(m, image);
    } else if(file->file_size > 79 && 0 == strncmp(file->file_data, "WOZ", 3)) {
        // WOZ file
        return image_load_woz(m, image);
    } else {
        if(file->file_size == 6656 * 35 || file->file_size == 6384 * 35 || (ext && 0 == stricmp(ext, ".nib"))) {
        // NIB file
            return image_load_nib(m, image);
        } else {
            return A2_ERR;
        }
    }

    return A2_OK;
}

void diskii_drive_select(APPLE2 *m, const int slot, int soft_switch) {
    m->diskii_controller[slot].active = soft_switch & 1;
}

void diskii_motor(APPLE2 *m, const int slot, int soft_switch) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    // Update the spin up/down timer
    diskii_timer_update(m->cpu.cycles, &d->motor_event_cycles, &d->motor_spinup_cycles);

    if(soft_switch & 1) {
        if(!d->motor_on) {
            d->motor_on = 1;
            d->motor_spinup_cycles = d->motor_spinup_cycles >= ms_to_cycles(400) ? 0 : ms_to_cycles(400) - d->motor_spinup_cycles;
        }
    } else {
        if(d->motor_on) {
            d->motor_on = 0;
            d->motor_spinup_cycles = ms_to_cycles(1000) - d->motor_spinup_cycles;
        }
    }
}

void diskii_step_head(APPLE2 *m, int slot, int soft_switch) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];

    diskii_timer_update(m->cpu.cycles, &d->head_event_cycles, &d->head_settle_cycles);

    uint8_t phase_bit = (1 << ((soft_switch >> 1) & 3)); // PH0..PH3
    uint8_t on = (soft_switch & 1);

    uint8_t prev_on = d->last_on_phase_mask;             // last non-zero on state
    uint8_t curr = on ? (d->phase_mask | phase_bit) : (d->phase_mask & ~phase_bit);

    if(curr == d->phase_mask) {
        return;                                          // no change
    }

    int n = count_phases(curr);
    if(n == 0 || n > 2) {                                // invalid holding pattern
        d->head_locked = 0;
        d->phase_mask  = curr;
        return;
    }

    // Determine direction relative to previous on mask.
    int step_qtr = (n == 1) ? 2 : 1;                     // 1-phase=1/2 track, 2-phase=1/4 track
    if(curr & ROL4(prev_on)) {
        // forward
    } else if(curr & ROR4(prev_on)) {
        step_qtr = -step_qtr;                            // reverse
    } else {
        d->head_locked = 1;                              // non-adjacent change -> lock
        d->phase_mask  = curr;
        if(curr) {
            d->last_on_phase_mask = curr;
        }
        return;
    }

    d->head_settle_cycles = ms_to_cycles(3);
    d->quater_track_pos += step_qtr;
    if(n == 1) {
        d->quater_track_pos &= ~1;                       // snap single-phase to even qtracks
    }
    if(d->quater_track_pos < 0) {
        d->quater_track_pos = 0;
    } else if(d->quater_track_pos >= DISKII_QUATERTRACKS) {
        d->quater_track_pos = DISKII_QUATERTRACKS - 1;
    }

    image_head_position(&d->image, d->quater_track_pos);

    d->phase_mask = curr;
    if(curr) {
        d->last_on_phase_mask = curr;                    // preserve across all off
    }
}

uint8_t diskii_q6_access(APPLE2 *m, int slot, uint8_t on_off) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    diskii_timer_update(m->cpu.cycles, &d->head_event_cycles, &d->head_settle_cycles);
    diskii_timer_update(m->cpu.cycles, &d->motor_event_cycles, &d->motor_spinup_cycles);
    if(m->cpu.cycles - d->q6_last_read_cycles < 32 || !d->motor_on || !d->image.image_specifics || d->head_settle_cycles || d->motor_spinup_cycles) {
        return 0x7f;
    }
    return image_get_byte(m, d);
}

uint8_t diskii_q7_access(APPLE2 *m, int slot, uint8_t on_off) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    return 0x7f;
}


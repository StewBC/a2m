// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

#define ms_to_cycles(x) (((x) * (uint64_t)CPU_FREQUENCY) / 1000)
#define us_to_cycles(x) (((x) * (uint64_t)CPU_FREQUENCY) / 1000000)
#define count_phases(x) ((x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1) + ((x >> 3) & 1))
#define ROL4(x)         (((x << 1) | (x >> 3)) & 0xF)
#define ROR4(x)         (((x >> 1) | (x << 3)) & 0xF)

#define DISKII_NORMAL_RPM       300
#define DISKII_READABLE_RPM     200
#define DISKII_SPINUP_TIME      ms_to_cycles(400)
#define DISKII_SPINDOWN_TIME    ms_to_cycles(1000)
#define DISKII_SPINUP_RATE      (double)DISKII_NORMAL_RPM / (double)DISKII_SPINUP_TIME    // RPM per cycle
#define DISKII_SPINDOWN_RATE    (double)DISKII_NORMAL_RPM / (double)DISKII_SPINDOWN_TIME

static inline double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static inline double rpm_now(uint64_t now, diskii_drive_t *d) {
    double dt = now - d->motor_event_cycles;
    double rate = d->motor_on ? DISKII_SPINUP_RATE : -DISKII_SPINDOWN_RATE;
    double rpm  = d->motor_rpm + rate * dt;
    return clamp(rpm, 0.0, DISKII_NORMAL_RPM);
}

static inline void diskii_timer_update(uint64_t now, uint64_t *anchor, uint64_t *timer) {
    uint64_t delta = now - *anchor;
    *anchor = now;
    *timer = *timer > delta ? *timer - delta : 0;
}

void diskii_drive_select(APPLE2 *m, const int slot, int soft_switch) {
    m->diskii_controller[slot].active = soft_switch & 1;
}

void diskii_motor(APPLE2 *m, const int slot, int soft_switch) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    uint64_t now = m->cpu.cycles;
    double current_rpm = rpm_now(now, d);
    d->motor_rpm = current_rpm;
    d->motor_event_cycles = now;
    d->motor_on = soft_switch & 1;
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
    int load;
    // SQW - Write a better image type detect
    if(file->file_size == 143360 || (ext && (0 == stricmp(ext, ".dsk") || 0 == stricmp(ext, ".do") || 0 == stricmp(ext, ".po")))) {
        // DSK file
        load = image_load_dsk(m, image, ext);
    } else if(file->file_size > 79 && 0 == strncmp(file->file_data, "WOZ", 3)) {
        // WOZ file
        load = image_load_woz(m, image);
    } else {
        if(file->file_size == 6656 * 35 || file->file_size == 6384 * 35 || (ext && 0 == stricmp(ext, ".nib"))) {
            // NIB file
            load = image_load_nib(m, image);
        } else {
            return A2_ERR;
        }
    }
    if(load != A2_OK) {
        return load;
    }
    // Map the appropriate ROM
    slot_add_card(m, slot, SLOT_TYPE_DISKII, &m->diskii_controller[slot],
                  m->roms.blocks[ROM_DISKII_13SECTOR + image->disk_encoding].bytes, NULL);

    return A2_OK;
}

uint8_t diskii_q6_access(APPLE2 *m, int slot, uint8_t on_off) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    diskii_timer_update(m->cpu.cycles, &d->head_event_cycles, &d->head_settle_cycles);
    if(m->cpu.cycles - d->q6_last_read_cycles < 32 || rpm_now(m->cpu.cycles, d) < DISKII_READABLE_RPM || !d->image.image_specifics || d->head_settle_cycles) {
        return 0x7f;
    }
    return image_get_byte(m, d);
}

uint8_t diskii_q7_access(APPLE2 *m, int slot, uint8_t on_off) {
    diskii_drive_t *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    return 0x7f;
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
            image_shutdown(&m->diskii_controller[slot].diskii_drive[0].image);
            image_shutdown(&m->diskii_controller[slot].diskii_drive[1].image);
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
        d->phase_mask  = curr;
        if(curr) {
            d->last_on_phase_mask = curr;
        }
        return;
    }

    d->head_settle_cycles = ms_to_cycles(3);
    d->quarter_track_pos += step_qtr;
    if(n == 1) {
        d->quarter_track_pos &= ~1;                       // snap single-phase to even qtracks
    }
    if(d->quarter_track_pos < 0) {
        d->quarter_track_pos = 0;
    } else if(d->quarter_track_pos >= DISKII_QUATERTRACKS) {
        d->quarter_track_pos = DISKII_QUATERTRACKS - 1;
    }

    image_head_position(&d->image, d->quarter_track_pos);

    d->phase_mask = curr;
    if(curr) {
        d->last_on_phase_mask = curr;                    // preserve across all off
    }
}

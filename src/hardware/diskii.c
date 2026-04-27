// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

// SQW
// cycles_per_byte = (CPU_FREQUENCY * 60 / target_RPM) / track_bytes

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

static inline double rpm_now(uint64_t now, DISKII_DRIVE *d) {
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

static uint8_t diskii_register_read(APPLE2 *m, DISKII_DRIVE *d) {
    if(!d->active_image || !d->active_image->image_specifics) {
        return 0x7f;
    }

    if(d->q7) {
        return d->q6 ? 0x7f : 0x80;
    }

    if(d->q6) {
        return d->sensor_protect ? 0x80 : 0x00;
    }

    diskii_timer_update(m->cpu.cycles, &d->head_event_cycles, &d->head_settle_cycles);
    if((double)m->cpu.cycles - d->q6_last_read_cycles < image_cycles_per_byte(d->active_image) ||
            rpm_now(m->cpu.cycles, d) < DISKII_READABLE_RPM || d->head_settle_cycles) {
        return 0x7f;
    }

    if(m->a2out_cb.cb_diskactivity_ctx.cb_diskread) {
        m->a2out_cb.cb_diskactivity_ctx.cb_diskread(m->a2out_cb.cb_diskactivity_ctx.user);
    }
    return image_get_byte(m, d);
}

static void diskii_write_latch_to_disk(APPLE2 *m, DISKII_DRIVE *d) {
    if(d->sensor_protect || !d->write_latch_valid) {
        return;
    }

    if(A2_OK == image_put_byte(m, d, d->write_latch) && m->a2out_cb.cb_diskactivity_ctx.cb_diskwrite) {
        m->a2out_cb.cb_diskactivity_ctx.cb_diskwrite(m->a2out_cb.cb_diskactivity_ctx.user);
    }
}

void diskii_controller_init(DISKII_CONTROLLER *controller) {
    ARRAY_INIT(&controller->diskii_drive[0].images, DISKII_IMAGE);
    ARRAY_INIT(&controller->diskii_drive[1].images, DISKII_IMAGE);
}

void diskii_drive_select(APPLE2 *m, const int slot, int soft_switch) {
    DISKII_CONTROLLER *dc = &m->diskii_controller[slot];
    DISKII_DRIVE *d = &dc->diskii_drive[dc->active];
    if(d->write_active) {
        image_finish_write(d);
        d->write_active = 0;
    }
    m->diskii_controller[slot].active = soft_switch & 1;
}

void diskii_motor(APPLE2 *m, const int slot, int soft_switch) {
    DISKII_DRIVE *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    uint64_t now = m->cpu.cycles;
    double current_rpm = rpm_now(now, d);
    d->motor_rpm = current_rpm;
    d->motor_event_cycles = now;
    d->motor_on = soft_switch & 1;
    if(!d->motor_on && d->write_active) {
        image_finish_write(d);
        d->write_active = 0;
    }
}

int diskii_eject(APPLE2 *m, const int slot, const int device, int mount_next) {
    DISKII_CONTROLLER *dc = &m->diskii_controller[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_DISKII) {
        return A2_ERR;
    }

    DISKII_DRIVE *dd = &dc->diskii_drive[device];
    if(!dd->active_image) {
        return A2_OK;
    }

    if(dd->write_active) {
        if(A2_OK != image_finish_write(dd)) {
            return A2_ERR;
        }
        dd->write_active = 0;
    }
    if(image_is_dirty(dd->active_image) && A2_OK != image_save(dd->active_image)) {
        return A2_ERR;
    }
    image_shutdown(dd->active_image);
    array_remove(&dd->images, dd->active_image);
    dd->active_image = NULL;
    int index = dd->image_index;
    size_t items = dd->images.items;
    if(mount_next && items > 0) {
        diskii_mount_image(m, slot, device, index >= items ? 0 : index);
    } else {
        dd->image_index = -1;
    }
    return A2_OK;
}

int diskii_mount(APPLE2 *m, const int slot, const int device, const char *file_name) {
    DISKII_CONTROLLER *dc = &m->diskii_controller[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_DISKII) {
        return A2_ERR;
    }
    DISKII_DRIVE *dd = &dc->diskii_drive[device];
    DISKII_IMAGE new_disk_image;
    memset(&new_disk_image, 0, sizeof(DISKII_IMAGE));
    DISKII_IMAGE *image = &new_disk_image;
    // DISKII_IMAGE *image = &dd->image;
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
    if(load != A2_OK || A2_ERR == ARRAY_ADD(&dd->images, new_disk_image)) {
        image_shutdown(&new_disk_image);
        return load;
    }
    dd->image_index = dd->images.items - 1;
    return diskii_mount_image(m, slot, device, dd->image_index);
}

uint8_t diskii_mount_image(APPLE2 *m, const int slot, const int device, const int index) {
    DISKII_CONTROLLER *dc = &m->diskii_controller[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_DISKII) {
        return A2_ERR;
    }
    DISKII_DRIVE *dd = &dc->diskii_drive[device];
    if(!dd->images.items || index > dd->images.items) {
        dd->image_index = -1;
        return A2_OK;
    }
    dd->active_image = ARRAY_GET(&dd->images, DISKII_IMAGE, index);
    // Map the appropriate ROM
    slot_add_card(m, slot, SLOT_TYPE_DISKII, &m->diskii_controller[slot],
                  m->roms.blocks[ROM_DISKII_13SECTOR + dd->active_image->disk_encoding].bytes, NULL);
    dd->quarter_track_pos = rand() % DISKII_QUATERTRACKS;
    dd->image_index = index;
    dd->write_latch_valid = 0;
    dd->write_active = 0;
    dd->write_track = 0;
    dd->write_start_pos = 0;
    dd->write_byte_count = 0;
    dd->q6 = 0;
    dd->q7 = 0;
    return A2_OK;
}

int diskii_save(APPLE2 *m, const int slot, const int device) {
    DISKII_CONTROLLER *dc = &m->diskii_controller[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_DISKII) {
        return A2_ERR;
    }

    DISKII_DRIVE *dd = &dc->diskii_drive[device];
    if(!dd->active_image) {
        return A2_OK;
    }
    if(dd->write_active) {
        if(A2_OK != image_finish_write(dd)) {
            return A2_ERR;
        }
        dd->write_active = 0;
    }
    return image_save(dd->active_image);
}

uint8_t diskii_q6_access(APPLE2 *m, int slot, uint8_t on_off) {
    DISKII_DRIVE *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    uint8_t old_q6 = d->q6;
    d->q6 = on_off;
    if(d->q7 && old_q6 && !d->q6) {
        diskii_write_latch_to_disk(m, d);
    }
    return diskii_register_read(m, d);
}

uint8_t diskii_q7_access(APPLE2 *m, int slot, uint8_t on_off) {
    DISKII_DRIVE *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    if(d->q7 && !on_off) {
        image_finish_write(d);
        d->write_active = 0;
    }
    d->q7 = on_off;
    return diskii_register_read(m, d);
}

void diskii_write_access(APPLE2 *m, int slot, uint8_t value) {
    DISKII_DRIVE *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];
    if(!d->active_image || !d->active_image->image_specifics || !d->q7 || !d->q6) {
        return;
    }

    d->write_latch = value;
    d->write_latch_valid = 1;
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
            DISKII_DRIVE *d0 = &m->diskii_controller[slot].diskii_drive[0];
            if(d0->write_active) {
                image_finish_write(d0);
                d0->write_active = 0;
            }
            for(int i = 0; i < d0->images.items; i++) {
                image_shutdown(ARRAY_GET(&d0->images, DISKII_IMAGE, i));
            }
            m->diskii_controller[slot].diskii_drive[0].active_image = NULL;
            DISKII_DRIVE *d1 = &m->diskii_controller[slot].diskii_drive[1];
            if(d1->write_active) {
                image_finish_write(d1);
                d1->write_active = 0;
            }
            for(int i = 0; i < d1->images.items; i++) {
                image_shutdown(ARRAY_GET(&d1->images, DISKII_IMAGE, i));
            }
            m->diskii_controller[slot].diskii_drive[1].active_image = NULL;
            array_free(&d0->images);
            array_free(&d1->images);
        }
    }
}

void diskii_step_head(APPLE2 *m, int slot, int soft_switch) {
    DISKII_DRIVE *d = &m->diskii_controller[slot].diskii_drive[m->diskii_controller[slot].active];

    diskii_timer_update(m->cpu.cycles, &d->head_event_cycles, &d->head_settle_cycles);

    uint8_t phase_bit = (1u << ((soft_switch >> 1) & 3)); // PH0..PH3
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

    if(d->active_image) {
        image_head_position(d->active_image, d->quarter_track_pos);
    }

    d->phase_mask = curr;
    if(curr) {
        d->last_on_phase_mask = curr;                    // preserve across all off
    }
}

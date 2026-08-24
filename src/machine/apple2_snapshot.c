#include "apple2_snapshot.h"

#include "a2_status.h"
#include "diskii.h"
#include "image.h"
#include "smrtprt.h"
#include "softswitch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define A2_ACCESS_OK 0
static int a2_path_exists(const char *path)
{
    return path != NULL && path[0] != '\0' && _access(path, 0) == 0;
}
#else
#include <unistd.h>
static int a2_path_exists(const char *path)
{
    return path != NULL && path[0] != '\0' && access(path, F_OK) == 0;
}
#endif

enum {
    A2_SNAPSHOT_HEADER_SIZE = 32,
    A2_SNAPSHOT_MAX_CHUNK_SIZE = 32u * 1024u * 1024u,
    A2_SNAPSHOT_MAX_PATH = 1024u
};

#define A2_SNAPSHOT_TAG(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

enum {
    TAG_META = A2_SNAPSHOT_TAG('M', 'E', 'T', 'A'),
    TAG_CPU_ = A2_SNAPSHOT_TAG('C', 'P', 'U', '_'),
    TAG_RAM_ = A2_SNAPSHOT_TAG('R', 'A', 'M', '_'),
    TAG_SOFT = A2_SNAPSHOT_TAG('S', 'O', 'F', 'T'),
    TAG_VID_ = A2_SNAPSHOT_TAG('V', 'I', 'D', '_'),
    TAG_SLOT = A2_SNAPSHOT_TAG('S', 'L', 'O', 'T'),
    TAG_DSKs = A2_SNAPSHOT_TAG('D', 'S', 'K', 's'),
    TAG_SPrt = A2_SNAPSHOT_TAG('S', 'P', 'r', 't'),
    TAG_MBrd = A2_SNAPSHOT_TAG('M', 'B', 'r', 'd')
};

typedef struct snapshot_writer {
    uint8_t *out;
    size_t cap;
    size_t pos;
    bool ok;
} snapshot_writer;

typedef struct snapshot_reader {
    const uint8_t *data;
    size_t len;
    size_t pos;
    bool ok;
} snapshot_reader;

typedef struct parsed_chunks {
    const uint8_t *meta;
    size_t meta_len;
    const uint8_t *cpu;
    size_t cpu_len;
    const uint8_t *ram;
    size_t ram_len;
    const uint8_t *soft;
    size_t soft_len;
    const uint8_t *vid;
    size_t vid_len;
    const uint8_t *slot;
    size_t slot_len;
    const uint8_t *dsks;
    size_t dsks_len;
    const uint8_t *sprt;
    size_t sprt_len;
    const uint8_t *mbrd;
    size_t mbrd_len;
    bool has_meta;
    bool has_cpu;
    bool has_ram;
    bool has_soft;
    bool has_vid;
    bool has_slot;
} parsed_chunks;

static void w_bytes(snapshot_writer *w, const void *data, size_t size)
{
    if (!w->ok) {
        return;
    }
    if (size > w->cap || w->pos > w->cap - size) {
        w->ok = false;
        return;
    }
    if (w->out != NULL && data != NULL && size > 0) {
        memcpy(w->out + w->pos, data, size);
    }
    w->pos += size;
}

static void w_u8(snapshot_writer *w, uint8_t value)
{
    w_bytes(w, &value, 1);
}

static void w_bool(snapshot_writer *w, bool value)
{
    w_u8(w, value ? 1u : 0u);
}

static void w_u16(snapshot_writer *w, uint16_t value)
{
    uint8_t b[2];
    b[0] = (uint8_t)(value & 0xffu);
    b[1] = (uint8_t)(value >> 8);
    w_bytes(w, b, 2);
}

static void w_u32(snapshot_writer *w, uint32_t value)
{
    uint8_t b[4];
    b[0] = (uint8_t)(value & 0xffu);
    b[1] = (uint8_t)((value >> 8) & 0xffu);
    b[2] = (uint8_t)((value >> 16) & 0xffu);
    b[3] = (uint8_t)(value >> 24);
    w_bytes(w, b, 4);
}

static void w_u64(snapshot_writer *w, uint64_t value)
{
    size_t i;
    for (i = 0; i < 8; ++i) {
        w_u8(w, (uint8_t)(value >> (i * 8u)));
    }
}

static void w_i16(snapshot_writer *w, int16_t value)
{
    w_u16(w, (uint16_t)value);
}

static void w_i32(snapshot_writer *w, int32_t value)
{
    w_u32(w, (uint32_t)value);
}

static void w_double(snapshot_writer *w, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    w_u64(w, bits);
}

static void w_float(snapshot_writer *w, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    w_u32(w, bits);
}

static void w_path(snapshot_writer *w, const char *path)
{
    size_t len = 0;
    if (path != NULL) {
        len = strlen(path);
        if (len > A2_SNAPSHOT_MAX_PATH) {
            w->ok = false;
            return;
        }
    }
    w_u16(w, (uint16_t)len);
    if (len > 0) {
        w_bytes(w, path, len);
    }
}

static void begin_chunk(snapshot_writer *w, uint32_t tag, size_t *len_pos)
{
    w_u32(w, tag);
    *len_pos = w->pos;
    w_u32(w, 0);
}

static void end_chunk(snapshot_writer *w, size_t len_pos)
{
    size_t payload_start = len_pos + sizeof(uint32_t);
    size_t payload_size;

    if (!w->ok || len_pos + sizeof(uint32_t) > w->pos) {
        w->ok = false;
        return;
    }
    payload_size = w->pos - payload_start;
    if (payload_size > UINT32_MAX) {
        w->ok = false;
        return;
    }
    if (w->out != NULL) {
        uint32_t value = (uint32_t)payload_size;
        w->out[len_pos + 0] = (uint8_t)(value & 0xffu);
        w->out[len_pos + 1] = (uint8_t)((value >> 8) & 0xffu);
        w->out[len_pos + 2] = (uint8_t)((value >> 16) & 0xffu);
        w->out[len_pos + 3] = (uint8_t)(value >> 24);
    }
}

static uint8_t r_u8(snapshot_reader *r)
{
    if (!r->ok || r->pos >= r->len) {
        r->ok = false;
        return 0;
    }
    return r->data[r->pos++];
}

static bool r_bool(snapshot_reader *r)
{
    return r_u8(r) != 0;
}

static uint16_t r_u16(snapshot_reader *r)
{
    uint16_t lo = r_u8(r);
    uint16_t hi = r_u8(r);
    return (uint16_t)(lo | (uint16_t)(hi << 8));
}

static uint32_t r_u32(snapshot_reader *r)
{
    uint32_t b0 = r_u8(r);
    uint32_t b1 = r_u8(r);
    uint32_t b2 = r_u8(r);
    uint32_t b3 = r_u8(r);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static uint64_t r_u64(snapshot_reader *r)
{
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < 8; ++i) {
        value |= (uint64_t)r_u8(r) << (i * 8u);
    }
    return value;
}

static int16_t r_i16(snapshot_reader *r)
{
    return (int16_t)r_u16(r);
}

static int32_t r_i32(snapshot_reader *r)
{
    return (int32_t)r_u32(r);
}

static double r_double(snapshot_reader *r)
{
    uint64_t bits = r_u64(r);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float r_float(snapshot_reader *r)
{
    uint32_t bits = r_u32(r);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void r_bytes(snapshot_reader *r, void *out, size_t size)
{
    if (!r->ok || size > r->len || r->pos > r->len - size) {
        r->ok = false;
        return;
    }
    if (size > 0 && out != NULL) {
        memcpy(out, r->data + r->pos, size);
    }
    r->pos += size;
}

static void r_path(snapshot_reader *r, char *out, size_t out_size)
{
    uint16_t len = r_u16(r);
    if (!r->ok || len > A2_SNAPSHOT_MAX_PATH || out_size == 0) {
        r->ok = false;
        return;
    }
    if ((size_t)len + 1u > out_size) {
        r->ok = false;
        return;
    }
    if (len > 0) {
        r_bytes(r, out, len);
    }
    if (r->ok) {
        out[len] = '\0';
    }
}

static void write_header(snapshot_writer *w, uint32_t flags)
{
    w_u32(w, A2_SNAPSHOT_MAGIC);
    w_u32(w, A2_SNAPSHOT_VERSION);
    w_u32(w, A2_SNAPSHOT_HEADER_SIZE);
    w_u32(w, (uint32_t)A2_SNAPSHOT_CONTENT_REFERENCED);
    w_u32(w, flags);
    w_u32(w, 0);
    w_u32(w, 0);
    w_u32(w, 0);
}

static uint32_t snapshot_flags_for_machine(const apple2_t *m)
{
    uint32_t flags = 0;
    int slot;
    int drive;

    for (slot = 1; slot <= 7; ++slot) {
        if (m->diskii_present[slot]) {
            for (drive = 0; drive < 2; ++drive) {
                if (m->diskii_controller[slot].diskii_drive[drive].images.items > 0) {
                    flags |= A2_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES;
                }
            }
        }
        if (m->slot_type[slot] == SLOT_TYPE_SMARTPORT) {
            if (sp_unit_mounted(&m->sp_device[slot], 0) ||
                sp_unit_mounted(&m->sp_device[slot], 1)) {
                flags |= A2_SNAPSHOT_FLAG_EXTERNAL_MEDIA_REFERENCES;
            }
        }
    }
    return flags;
}

bool apple2_snapshot_flush_media(apple2_t *m)
{
    int slot;
    int drive;

    if (m == NULL) {
        return false;
    }
    for (slot = 1; slot <= 7; ++slot) {
        if (!m->diskii_present[slot]) {
            continue;
        }
        for (drive = 0; drive < 2; ++drive) {
            DISKII_DRIVE *dd = &m->diskii_controller[slot].diskii_drive[drive];
            if (dd->write_active) {
                if (image_finish_write(m, dd) != A2_OK) {
                    return false;
                }
                dd->write_active = 0;
            }
            if (dd->active_image != NULL && image_is_dirty(dd->active_image)) {
                if (image_save(dd->active_image) != A2_OK) {
                    return false;
                }
            }
            /* Also flush non-active queued images that are dirty. */
            {
                size_t i;
                for (i = 0; i < dd->images.items; ++i) {
                    DISKII_IMAGE *img = ARRAY_GET(&dd->images, DISKII_IMAGE, i);
                    if (img != NULL && image_is_dirty(img)) {
                        if (image_save(img) != A2_OK) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

static void write_meta(snapshot_writer *w, const apple2_t *m, uint32_t flags)
{
    size_t chunk;
    begin_chunk(w, TAG_META, &chunk);
    w_u32(w, flags);
    w_u32(w, (uint32_t)A2_SNAPSHOT_CONTENT_REFERENCED);
    w_u8(w, (uint8_t)m->model);
    w_u8(w, m->mb_slot);
    w_u8(w, 0); /* reserved */
    w_u8(w, 0);
    end_chunk(w, chunk);
}

static void write_cpu(snapshot_writer *w, const apple2_t *m)
{
    const cpu65_t *c = &m->cpu;
    const CPU *cpu = &c->cpu;
    size_t chunk;

    begin_chunk(w, TAG_CPU_, &chunk);
    w_u16(w, cpu->pc);
    w_u16(w, cpu->opcode_pc);
    w_u16(w, cpu->sp);
    w_u8(w, cpu->A);
    w_u8(w, cpu->X);
    w_u8(w, cpu->Y);
    w_u8(w, cpu->flags);
    w_u16(w, cpu->address_16);
    w_u16(w, cpu->scratch_16);
    w_u8(w, cpu->page_fault);
    w_u8(w, cpu->irq_defer);
    w_u8(w, cpu->irq_defer_i);
    w_u8(w, cpu->opcode_active);
    w_u32(w, cpu->class);
    w_u64(w, cpu->cycles);
    w_u64(w, cpu->irq_entries);
    w_u64(w, cpu->nmi_entries);
    w_u8(w, (uint8_t)c->bus_access_kind);
    w_u8(w, c->micro_active);
    w_u8(w, c->micro_opcode);
    w_u8(w, c->micro_phase);
    w_u8(w, c->micro_branch_taken);
    w_u16(w, c->micro_target);
    w_u16(w, c->micro_interrupt_vector);
    w_u8(w, c->micro_is_interrupt);
    w_bool(w, m->ready);
    w_bool(w, m->instruction_complete);
    w_u32(w, m->prng); /* v2: machine-local PRNG. v1 loaders never see this. */
    end_chunk(w, chunk);
}

static void write_ram(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    begin_chunk(w, TAG_RAM_, &chunk);
    w_u32(w, (uint32_t)APPLE2_RAM_MAIN_SIZE);
    w_u32(w, (uint32_t)APPLE2_RAM_LC_SIZE);
    w_bytes(w, m->ram_main, APPLE2_RAM_MAIN_SIZE);
    w_bytes(w, m->ram_lc, APPLE2_RAM_LC_SIZE);
    end_chunk(w, chunk);
}

static void write_soft(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    int i;
    begin_chunk(w, TAG_SOFT, &chunk);
    w_u32(w, m->state_flags);
    w_u8(w, m->key_held);
    w_i32(w, m->strobed_slot);
    w_bool(w, m->speaker_level);
    for (i = 0; i < 4; ++i) {
        w_u8(w, m->gameport_axis[i]);
    }
    w_u8(w, m->gameport_buttons);
    w_u64(w, m->gameport_ptrig_cycle);
    w_i32(w, m->c800_card);
    end_chunk(w, chunk);
}

static void write_vid(snapshot_writer *w, const apple2_t *m)
{
    const apple2_video *v = &m->video;
    size_t chunk;
    begin_chunk(w, TAG_VID_, &chunk);
    w_u16(w, v->cycle_in_line);
    w_u16(w, v->line);
    w_u64(w, v->frame_number);
    w_u32(w, v->frame_gen);
    w_u8(w, v->last_video_byte);
    w_bool(w, v->paint_enabled);
    end_chunk(w, chunk);
}

static void write_slot(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    int slot;
    begin_chunk(w, TAG_SLOT, &chunk);
    for (slot = 0; slot < 8; ++slot) {
        w_u8(w, (uint8_t)m->slot_type[slot]);
        w_u8(w, m->diskii_present[slot]);
    }
    w_u8(w, m->mb_slot);
    end_chunk(w, chunk);
}

static void write_drive_mech(snapshot_writer *w, const DISKII_DRIVE *dd)
{
    w_u64(w, dd->motor_event_cycles);
    w_u64(w, dd->motor_off_delay_cycles);
    w_double(w, dd->motor_rpm);
    w_u8(w, dd->motor_on);
    w_u64(w, dd->head_event_cycles);
    w_u8(w, dd->phase_mask);
    w_u8(w, dd->last_on_phase_mask);
    w_i16(w, dd->quarter_track_pos);
    w_u64(w, dd->head_settle_cycles);
    w_u8(w, dd->q6);
    w_u8(w, dd->q7);
    w_u8(w, dd->sensor_protect);
    w_double(w, dd->q6_last_read_cycles);
    w_u8(w, dd->read_latch);
    w_u8(w, dd->write_latch);
    w_u8(w, dd->write_latch_valid);
    w_u8(w, dd->write_active);
    w_u32(w, dd->write_track);
    w_u32(w, dd->write_start_pos);
    w_u32(w, dd->write_byte_count);
    w_i32(w, dd->image_index);
}

static void write_dsks(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    int slot;
    int drive;

    begin_chunk(w, TAG_DSKs, &chunk);
    for (slot = 1; slot <= 7; ++slot) {
        if (!m->diskii_present[slot] && m->slot_type[slot] != SLOT_TYPE_DISKII) {
            continue;
        }
        w_u8(w, (uint8_t)slot);
        w_u8(w, m->diskii_controller[slot].active);
        w_u64(w, m->diskii_controller[slot].cycles_at_update);
        for (drive = 0; drive < 2; ++drive) {
            const DISKII_DRIVE *dd = &m->diskii_controller[slot].diskii_drive[drive];
            size_t n = dd->images.items;
            size_t i;
            w_u16(w, (uint16_t)n);
            for (i = 0; i < n; ++i) {
                DISKII_IMAGE *img = ARRAY_GET(&dd->images, DISKII_IMAGE, i);
                const char *path =
                    (img != NULL && img->file.file_path != NULL) ? img->file.file_path : "";
                w_path(w, path);
            }
            write_drive_mech(w, dd);
        }
    }
    w_u8(w, 0); /* end marker */
    end_chunk(w, chunk);
}

static void write_sprt(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    int slot;
    int dev;

    begin_chunk(w, TAG_SPrt, &chunk);
    for (slot = 1; slot <= 7; ++slot) {
        if (m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
            continue;
        }
        w_u8(w, (uint8_t)slot);
        w_u8(w, m->sp_device[slot].sp_status);
        w_u64(w, (uint64_t)m->sp_device[slot].sp_read_offset);
        w_u64(w, (uint64_t)m->sp_device[slot].sp_write_offset);
        w_bytes(w, m->sp_device[slot].sp_buffer, sizeof(m->sp_device[slot].sp_buffer));
        for (dev = 0; dev < 2; ++dev) {
            const char *path = sp_unit_path(&m->sp_device[slot], dev);
            if (path == NULL) {
                path = "";
            }
            w_path(w, path);
            w_u64(w, (uint64_t)m->sp_device[slot].file_header_size[dev]);
        }
    }
    w_u8(w, 0);
    end_chunk(w, chunk);
}

static void write_via(snapshot_writer *w, const VIA6522 *via)
{
    w_u8(w, via->orb);
    w_u8(w, via->ora);
    w_u8(w, via->ddrb);
    w_u8(w, via->ddra);
    w_u8(w, via->acr);
    w_u8(w, via->pcr);
    w_u8(w, via->ifr);
    w_u8(w, via->ier);
    w_u8(w, via->sr);
    w_u8(w, via->t1_latch_lo);
    w_u8(w, via->t1_latch_hi);
    w_u8(w, via->t2_latch_lo);
    w_u8(w, via->t2_latch_hi);
    w_u16(w, via->t1_counter);
    w_u16(w, via->t1_latch);
    w_u16(w, via->t2_counter);
    w_u16(w, via->t2_latch);
    w_u64(w, via->timer_last_cycle);
    w_u64(w, via->t1_load_cycle);
    w_u64(w, via->t1_irq_visible_cycle);
    w_u64(w, via->t2_load_cycle);
    w_u8(w, via->t1_read_hi);
    w_u8(w, via->t1_read_latched);
    w_u8(w, via->t1_running);
    w_u8(w, via->t1_irq_armed);
    w_u8(w, via->t2_irq_armed);
    w_u8(w, via->t2_running);
    w_u8(w, via->t1_fired);
    w_u8(w, via->t2_fired);
    w_u8(w, via->t1_reload_pending);
    w_u8(w, via->t1_just_loaded);
    w_u8(w, via->t2_just_loaded);
    w_u8(w, via->port_b_input);
    w_u8(w, via->pb6_level);
    w_u8(w, via->ca1_level);
    w_u8(w, via->ca2_level);
    w_u8(w, via->cb1_level);
    w_u8(w, via->cb2_level);
    w_u8(w, via->ca2_pulse_pending);
    w_u8(w, via->cb2_pulse_pending);
    w_u8(w, via->sr_active);
    w_u8(w, via->sr_shift_count);
    w_u16(w, via->sr_t2_ticks_remaining);
    w_u8(w, via->board_t2_startup_free_run);
}

static void write_ay(snapshot_writer *w, const AY38910 *ay)
{
    int i;
    w_bytes(w, ay->regs, sizeof(ay->regs));
    w_u8(w, ay->selected_reg);
    w_u8(w, ay->selected_reg_valid);
    w_u8(w, ay->active);
    w_u8(w, ay->chip_rate_identity);
    w_double(w, ay->cpu_hz);
    w_double(w, ay->chip_hz);
    w_double(w, ay->chip_cycles_per_cpu_cycle);
    w_double(w, ay->chip_cycle_accum);
    for (i = 0; i < 3; ++i) {
        w_u16(w, ay->tone_period[i]);
        w_u16(w, ay->tone_counter[i]);
        w_u8(w, ay->tone_output[i]);
    }
    w_u32(w, ay->noise_period);
    w_u32(w, ay->noise_counter);
    w_u32(w, ay->noise_lfsr);
    w_u8(w, ay->noise_output);
    w_u32(w, ay->env_period);
    w_u32(w, ay->env_counter);
    w_u8(w, ay->env_level);
    w_u8(w, ay->env_continue);
    w_u8(w, ay->env_hold);
    w_u8(w, ay->env_alternate);
    w_u8(w, (uint8_t)ay->env_delta);
    w_u8(w, ay->env_holding);
    w_float(w, ay->sample);
}

static void write_mbrd(snapshot_writer *w, const apple2_t *m)
{
    size_t chunk;
    int slot;
    int pair;

    begin_chunk(w, TAG_MBrd, &chunk);
    for (slot = 1; slot <= 7; ++slot) {
        if (m->slot_type[slot] != SLOT_TYPE_MOCKINGBOARD) {
            continue;
        }
        w_u8(w, (uint8_t)slot);
        for (pair = 0; pair < 2; ++pair) {
            write_via(w, &m->mockingboard[slot].via[pair]);
            write_ay(w, &m->mockingboard[slot].ay[pair]);
            w_u32(w, m->mockingboard[slot].ay_pending_cycles[pair]);
            w_u8(w, m->mockingboard[slot].ay_bus_state[pair]);
        }
        w_u8(w, m->mockingboard[slot].board_startup_timer_seed_disabled);
    }
    w_u8(w, 0);
    end_chunk(w, chunk);
}

static void write_all(snapshot_writer *w, const apple2_t *m)
{
    uint32_t flags = snapshot_flags_for_machine(m);
    write_header(w, flags);
    write_meta(w, m, flags);
    write_cpu(w, m);
    write_ram(w, m);
    write_soft(w, m);
    write_vid(w, m);
    write_slot(w, m);
    write_dsks(w, m);
    write_sprt(w, m);
    write_mbrd(w, m);
}

size_t apple2_snapshot_size(const apple2_t *m)
{
    snapshot_writer w;

    if (m == NULL || m->ram_main == NULL || m->ram_lc == NULL) {
        return 0;
    }
    memset(&w, 0, sizeof(w));
    w.cap = (size_t)-1;
    w.ok = true;
    write_all(&w, m);
    return w.ok ? w.pos : 0;
}

size_t apple2_snapshot_save(const apple2_t *m, uint8_t *out, size_t out_cap)
{
    snapshot_writer w;

    if (m == NULL || m->ram_main == NULL || m->ram_lc == NULL) {
        return 0;
    }
    /* Flush is required before save; caller (runtime) should call
       apple2_snapshot_flush_media on a mutable machine first. Const here
       matches c64m API; flush is done by runtime before this call. */
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.cap = out_cap;
    w.ok = true;
    write_all(&w, m);
    return (w.ok && w.pos <= out_cap) ? w.pos : 0;
}

static bool parse_chunks(const uint8_t *in, size_t in_len, parsed_chunks *out)
{
    snapshot_reader r;
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;

    memset(out, 0, sizeof(*out));
    if (in == NULL || in_len < A2_SNAPSHOT_HEADER_SIZE) {
        return false;
    }
    memset(&r, 0, sizeof(r));
    r.data = in;
    r.len = in_len;
    r.ok = true;

    magic = r_u32(&r);
    version = r_u32(&r);
    header_size = r_u32(&r);
    (void)r_u32(&r); /* content mode */
    (void)r_u32(&r); /* flags */
    (void)r_u32(&r);
    (void)r_u32(&r);
    (void)r_u32(&r);

    if (!r.ok || magic != A2_SNAPSHOT_MAGIC || version < A2_SNAPSHOT_VERSION_MIN ||
        version > A2_SNAPSHOT_VERSION || header_size != A2_SNAPSHOT_HEADER_SIZE) {
        return false;
    }
    r.pos = header_size;

    while (r.ok && r.pos + 8 <= r.len) {
        uint32_t tag = r_u32(&r);
        uint32_t clen = r_u32(&r);
        const uint8_t *payload;
        if (!r.ok || clen > A2_SNAPSHOT_MAX_CHUNK_SIZE || r.pos + clen > r.len) {
            return false;
        }
        payload = r.data + r.pos;
        r.pos += clen;
        switch (tag) {
        case TAG_META:
            out->meta = payload;
            out->meta_len = clen;
            out->has_meta = true;
            break;
        case TAG_CPU_:
            out->cpu = payload;
            out->cpu_len = clen;
            out->has_cpu = true;
            break;
        case TAG_RAM_:
            out->ram = payload;
            out->ram_len = clen;
            out->has_ram = true;
            break;
        case TAG_SOFT:
            out->soft = payload;
            out->soft_len = clen;
            out->has_soft = true;
            break;
        case TAG_VID_:
            out->vid = payload;
            out->vid_len = clen;
            out->has_vid = true;
            break;
        case TAG_SLOT:
            out->slot = payload;
            out->slot_len = clen;
            out->has_slot = true;
            break;
        case TAG_DSKs:
            out->dsks = payload;
            out->dsks_len = clen;
            break;
        case TAG_SPrt:
            out->sprt = payload;
            out->sprt_len = clen;
            break;
        case TAG_MBrd:
            out->mbrd = payload;
            out->mbrd_len = clen;
            break;
        default:
            /* Unknown chunk: skip */
            break;
        }
    }
    return r.ok && out->has_meta && out->has_cpu && out->has_ram && out->has_soft &&
           out->has_vid && out->has_slot;
}

static bool apply_meta(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    uint8_t model;
    uint8_t mb_slot;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    (void)r_u32(&r);
    (void)r_u32(&r);
    model = r_u8(&r);
    mb_slot = r_u8(&r);
    (void)r_u8(&r);
    (void)r_u8(&r);
    if (!r.ok || (model != APPLE2_MODEL_II_PLUS && model != APPLE2_MODEL_IIE_ENHANCED)) {
        return false;
    }
    apple2_set_model(m, (apple2_model)model);
    m->mb_slot = mb_slot;
    return r.ok;
}

static bool apply_cpu(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    cpu65_t *c = &m->cpu;
    CPU *cpu = &c->cpu;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    cpu->pc = r_u16(&r);
    cpu->opcode_pc = r_u16(&r);
    cpu->sp = r_u16(&r);
    cpu->A = r_u8(&r);
    cpu->X = r_u8(&r);
    cpu->Y = r_u8(&r);
    cpu->flags = r_u8(&r);
    cpu->address_16 = r_u16(&r);
    cpu->scratch_16 = r_u16(&r);
    cpu->page_fault = r_u8(&r) & 1u;
    cpu->irq_defer = r_u8(&r);
    cpu->irq_defer_i = r_u8(&r);
    cpu->opcode_active = r_u8(&r);
    cpu->class = r_u32(&r);
    cpu->cycles = r_u64(&r);
    cpu->irq_entries = r_u64(&r);
    cpu->nmi_entries = r_u64(&r);
    c->bus_access_kind = (cpu65_bus_access_kind)r_u8(&r);
    c->micro_active = r_u8(&r);
    c->micro_opcode = r_u8(&r);
    c->micro_phase = r_u8(&r);
    c->micro_branch_taken = r_u8(&r);
    c->micro_target = r_u16(&r);
    c->micro_interrupt_vector = r_u16(&r);
    c->micro_is_interrupt = r_u8(&r);
    m->ready = r_bool(&r);
    m->instruction_complete = r_bool(&r);
    /* v2+: PRNG. v1 CPU_ chunks end here; keep the init seed. */
    if (r.ok && r.pos + 4u <= r.len) {
        m->prng = r_u32(&r);
        if (m->prng == 0u) {
            m->prng = 0xA2A2A2A2u;
        }
    }
    /* Bus read/write/irq callbacks stay as set by apple2_init (static in apple2.c). */
    c->user = m;
    return r.ok;
}

static bool apply_ram(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    uint32_t main_size;
    uint32_t lc_size;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    main_size = r_u32(&r);
    lc_size = r_u32(&r);
    if (!r.ok || main_size != APPLE2_RAM_MAIN_SIZE || lc_size != APPLE2_RAM_LC_SIZE) {
        return false;
    }
    r_bytes(&r, m->ram_main, APPLE2_RAM_MAIN_SIZE);
    r_bytes(&r, m->ram_lc, APPLE2_RAM_LC_SIZE);
    return r.ok;
}

static bool apply_soft(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    int i;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    m->state_flags = r_u32(&r);
    m->key_held = r_u8(&r);
    m->strobed_slot = r_i32(&r);
    m->last_io_select_slot = 0;
    m->speaker_level = r_bool(&r);
    for (i = 0; i < 4; ++i) {
        m->gameport_axis[i] = r_u8(&r);
    }
    m->gameport_buttons = r_u8(&r);
    m->gameport_ptrig_cycle = r_u64(&r);
    if (r.ok && r.pos + 4u <= r.len) {
        m->c800_card = r_i32(&r);
    } else if (m->strobed_slot >= 1 && m->strobed_slot <= 7) {
        m->c800_card = m->strobed_slot;
    } else {
        m->c800_card = -1;
    }
    m->c800_internal = (m->strobed_slot == 8);
    return r.ok;
}

static bool apply_vid(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    apple2_video *v = &m->video;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    v->cycle_in_line = r_u16(&r);
    v->line = r_u16(&r);
    v->frame_number = r_u64(&r);
    v->frame_gen = r_u32(&r);
    v->last_video_byte = r_u8(&r);
    v->paint_enabled = r_bool(&r);
    v->frame_ready = false;
    return r.ok;
}

static bool clear_diskii_drive(apple2_t *m, int slot, int drive)
{
    DISKII_DRIVE *dd = &m->diskii_controller[slot].diskii_drive[drive];

    while (dd->images.items > 0) {
        dd->active_image = ARRAY_GET(&dd->images, DISKII_IMAGE, 0);
        dd->image_index = 0;
        if (diskii_eject(m, slot, drive, 0) != A2_OK) {
            /* Force clear if eject fails mid-write finish. */
            if (dd->active_image != NULL) {
                image_shutdown(dd->active_image);
                array_remove(&dd->images, dd->active_image);
            } else {
                break;
            }
        }
    }
    dd->active_image = NULL;
    dd->image_index = -1;
    return true;
}

static bool apply_slot(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;
    uint8_t types[8];
    uint8_t present[8];
    uint8_t mb_slot;
    int slot;

    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;
    for (slot = 0; slot < 8; ++slot) {
        types[slot] = r_u8(&r);
        present[slot] = r_u8(&r);
    }
    mb_slot = r_u8(&r);
    if (!r.ok) {
        return false;
    }

    /* Tear down cards that will change; remount media after. */
    for (slot = 1; slot <= 7; ++slot) {
        clear_diskii_drive(m, slot, 0);
        clear_diskii_drive(m, slot, 1);
        if (m->slot_type[slot] != SLOT_TYPE_EMPTY &&
            m->slot_type[slot] != (apple2_slot_type)types[slot]) {
            apple2_detach_slot_card(m, slot);
        }
    }
    sp_shutdown(m);

    for (slot = 1; slot <= 7; ++slot) {
        apple2_slot_type want = (apple2_slot_type)types[slot];
        if (m->slot_type[slot] == want) {
            continue;
        }
        switch (want) {
        case SLOT_TYPE_DISKII:
            if (!apple2_attach_diskii(m, slot)) {
                return false;
            }
            break;
        case SLOT_TYPE_SMARTPORT:
            if (!apple2_attach_smartport(m, slot)) {
                return false;
            }
            break;
        case SLOT_TYPE_MOCKINGBOARD:
            if (!apple2_attach_mockingboard(m, slot)) {
                return false;
            }
            break;
        case SLOT_TYPE_EMPTY:
        default:
            apple2_detach_slot_card(m, slot);
            break;
        }
        m->diskii_present[slot] = present[slot] ? 1u : 0u;
    }
    m->mb_slot = mb_slot;
    return true;
}

static void read_drive_mech(snapshot_reader *r, DISKII_DRIVE *dd)
{
    dd->motor_event_cycles = r_u64(r);
    dd->motor_off_delay_cycles = r_u64(r);
    dd->motor_rpm = r_double(r);
    dd->motor_on = r_u8(r);
    dd->head_event_cycles = r_u64(r);
    dd->phase_mask = r_u8(r);
    dd->last_on_phase_mask = r_u8(r);
    dd->quarter_track_pos = r_i16(r);
    dd->head_settle_cycles = r_u64(r);
    dd->q6 = r_u8(r);
    dd->q7 = r_u8(r);
    dd->sensor_protect = r_u8(r);
    dd->q6_last_read_cycles = r_double(r);
    dd->read_latch = r_u8(r);
    dd->write_latch = r_u8(r);
    dd->write_latch_valid = r_u8(r);
    dd->write_active = r_u8(r);
    dd->write_track = r_u32(r);
    dd->write_start_pos = r_u32(r);
    dd->write_byte_count = r_u32(r);
    dd->image_index = r_i32(r);
}

static bool apply_dsks(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;

    if (p == NULL || len == 0) {
        return true;
    }
    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;

    for (;;) {
        uint8_t slot;
        int drive;
        if (!r.ok) {
            return false;
        }
        if (r.pos >= r.len) {
            break;
        }
        slot = r_u8(&r);
        if (slot == 0) {
            break;
        }
        if (slot > 7) {
            return false;
        }
        if (!m->diskii_present[slot] && m->slot_type[slot] != SLOT_TYPE_DISKII) {
            if (!apple2_attach_diskii(m, slot)) {
                return false;
            }
        }
        m->diskii_controller[slot].active = r_u8(&r);
        m->diskii_controller[slot].cycles_at_update = r_u64(&r);
        for (drive = 0; drive < 2; ++drive) {
            DISKII_DRIVE *dd = &m->diskii_controller[slot].diskii_drive[drive];
            uint16_t n = r_u16(&r);
            uint16_t i;
            char paths[16][A2_SNAPSHOT_MAX_PATH + 1];
            int saved_index;
            DISKII_DRIVE mech;

            if (n > 16) {
                return false;
            }
            memset(paths, 0, sizeof(paths));
            for (i = 0; i < n; ++i) {
                r_path(&r, paths[i], sizeof(paths[i]));
                if (!r.ok) {
                    return false;
                }
                if (paths[i][0] != '\0' && !a2_path_exists(paths[i])) {
                    return false; /* hard fail missing media */
                }
            }
            memset(&mech, 0, sizeof(mech));
            read_drive_mech(&r, &mech);
            if (!r.ok) {
                return false;
            }
            saved_index = mech.image_index;

            clear_diskii_drive(m, slot, drive);
            for (i = 0; i < n; ++i) {
                if (paths[i][0] == '\0') {
                    continue;
                }
                if (diskii_mount(m, slot, drive, paths[i]) != A2_OK) {
                    return false;
                }
            }
            /* Restore mechanical state after mount (mount randomizes head). */
            {
                int img_index = saved_index;
                int count = (int)dd->images.items;
                if (count > 0) {
                    if (img_index < 0 || img_index >= count) {
                        img_index = 0;
                    }
                    if (diskii_mount_image(m, slot, drive, img_index) != A2_OK) {
                        return false;
                    }
                }
            }
            dd->motor_event_cycles = mech.motor_event_cycles;
            dd->motor_off_delay_cycles = mech.motor_off_delay_cycles;
            dd->motor_rpm = mech.motor_rpm;
            dd->motor_on = mech.motor_on;
            dd->head_event_cycles = mech.head_event_cycles;
            dd->phase_mask = mech.phase_mask;
            dd->last_on_phase_mask = mech.last_on_phase_mask;
            dd->quarter_track_pos = mech.quarter_track_pos;
            dd->head_settle_cycles = mech.head_settle_cycles;
            dd->q6 = mech.q6;
            dd->q7 = mech.q7;
            dd->sensor_protect = mech.sensor_protect;
            dd->q6_last_read_cycles = mech.q6_last_read_cycles;
            dd->read_latch = mech.read_latch;
            dd->write_latch = mech.write_latch;
            dd->write_latch_valid = mech.write_latch_valid;
            /* Do not resume an in-progress write from snapshot. */
            dd->write_active = 0;
            dd->write_track = 0;
            dd->write_start_pos = 0;
            dd->write_byte_count = 0;
            if (dd->active_image != NULL) {
                image_head_position(dd->active_image, (uint32_t)dd->quarter_track_pos);
            }
        }
    }
    return r.ok;
}

static bool apply_sprt(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;

    if (p == NULL || len == 0) {
        return true;
    }
    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;

    for (;;) {
        uint8_t slot;
        int dev;
        char paths[2][A2_SNAPSHOT_MAX_PATH + 1];
        uint64_t headers[2];
        uint8_t status;
        uint64_t read_off;
        uint64_t write_off;
        uint8_t buffer[512 + 4];

        if (r.pos >= r.len) {
            break;
        }
        slot = r_u8(&r);
        if (slot == 0) {
            break;
        }
        if (slot > 7) {
            return false;
        }
        status = r_u8(&r);
        read_off = r_u64(&r);
        write_off = r_u64(&r);
        r_bytes(&r, buffer, sizeof(buffer));
        for (dev = 0; dev < 2; ++dev) {
            r_path(&r, paths[dev], sizeof(paths[dev]));
            headers[dev] = r_u64(&r);
            if (!r.ok) {
                return false;
            }
            if (paths[dev][0] != '\0' && !a2_path_exists(paths[dev])) {
                return false;
            }
        }
        if (m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
            if (!apple2_attach_smartport(m, slot)) {
                return false;
            }
        }
        for (dev = 0; dev < 2; ++dev) {
            if (paths[dev][0] == '\0') {
                continue;
            }
            if (sp_mount(m, slot, dev, paths[dev]) != A2_OK) {
                return false;
            }
            m->sp_device[slot].file_header_size[dev] = (size_t)headers[dev];
        }
        m->sp_device[slot].sp_status = status;
        m->sp_device[slot].sp_read_offset = (size_t)read_off;
        m->sp_device[slot].sp_write_offset = (size_t)write_off;
        memcpy(m->sp_device[slot].sp_buffer, buffer, sizeof(buffer));
    }
    return r.ok;
}

static void read_via(snapshot_reader *r, VIA6522 *via, apple2_t *m, uint8_t slot, uint8_t pair)
{
    via->owner = m;
    via->slot = slot;
    via->pair_index = pair;
    via->orb = r_u8(r);
    via->ora = r_u8(r);
    via->ddrb = r_u8(r);
    via->ddra = r_u8(r);
    via->acr = r_u8(r);
    via->pcr = r_u8(r);
    via->ifr = r_u8(r);
    via->ier = r_u8(r);
    via->sr = r_u8(r);
    via->t1_latch_lo = r_u8(r);
    via->t1_latch_hi = r_u8(r);
    via->t2_latch_lo = r_u8(r);
    via->t2_latch_hi = r_u8(r);
    via->t1_counter = r_u16(r);
    via->t1_latch = r_u16(r);
    via->t2_counter = r_u16(r);
    via->t2_latch = r_u16(r);
    via->timer_last_cycle = r_u64(r);
    via->t1_load_cycle = r_u64(r);
    via->t1_irq_visible_cycle = r_u64(r);
    via->t2_load_cycle = r_u64(r);
    via->t1_read_hi = r_u8(r);
    via->t1_read_latched = r_u8(r);
    via->t1_running = r_u8(r);
    via->t1_irq_armed = r_u8(r);
    via->t2_irq_armed = r_u8(r);
    via->t2_running = r_u8(r);
    via->t1_fired = r_u8(r);
    via->t2_fired = r_u8(r);
    via->t1_reload_pending = r_u8(r);
    via->t1_just_loaded = r_u8(r);
    via->t2_just_loaded = r_u8(r);
    via->port_b_input = r_u8(r);
    via->pb6_level = r_u8(r);
    via->ca1_level = r_u8(r);
    via->ca2_level = r_u8(r);
    via->cb1_level = r_u8(r);
    via->cb2_level = r_u8(r);
    via->ca2_pulse_pending = r_u8(r);
    via->cb2_pulse_pending = r_u8(r);
    via->sr_active = r_u8(r);
    via->sr_shift_count = r_u8(r);
    via->sr_t2_ticks_remaining = r_u16(r);
    via->board_t2_startup_free_run = r_u8(r);
}

static void read_ay(snapshot_reader *r, AY38910 *ay)
{
    int i;
    r_bytes(r, ay->regs, sizeof(ay->regs));
    ay->selected_reg = r_u8(r);
    ay->selected_reg_valid = r_u8(r);
    ay->active = r_u8(r);
    ay->chip_rate_identity = r_u8(r);
    ay->cpu_hz = r_double(r);
    ay->chip_hz = r_double(r);
    ay->chip_cycles_per_cpu_cycle = r_double(r);
    ay->chip_cycle_accum = r_double(r);
    for (i = 0; i < 3; ++i) {
        ay->tone_period[i] = r_u16(r);
        ay->tone_counter[i] = r_u16(r);
        ay->tone_output[i] = r_u8(r);
    }
    ay->noise_period = r_u32(r);
    ay->noise_counter = r_u32(r);
    ay->noise_lfsr = r_u32(r);
    ay->noise_output = r_u8(r);
    ay->env_period = r_u32(r);
    ay->env_counter = r_u32(r);
    ay->env_level = r_u8(r);
    ay->env_continue = r_u8(r);
    ay->env_hold = r_u8(r);
    ay->env_alternate = r_u8(r);
    ay->env_delta = (int8_t)r_u8(r);
    ay->env_holding = r_u8(r);
    ay->sample = r_float(r);
}

static bool apply_mbrd(apple2_t *m, const uint8_t *p, size_t len)
{
    snapshot_reader r;

    if (p == NULL || len == 0) {
        return true;
    }
    memset(&r, 0, sizeof(r));
    r.data = p;
    r.len = len;
    r.ok = true;

    for (;;) {
        uint8_t slot;
        int pair;
        if (r.pos >= r.len) {
            break;
        }
        slot = r_u8(&r);
        if (slot == 0) {
            break;
        }
        if (slot > 7) {
            return false;
        }
        if (m->slot_type[slot] != SLOT_TYPE_MOCKINGBOARD) {
            if (!apple2_attach_mockingboard(m, slot)) {
                return false;
            }
        }
        for (pair = 0; pair < 2; ++pair) {
            read_via(&r, &m->mockingboard[slot].via[pair], m, slot, (uint8_t)pair);
            read_ay(&r, &m->mockingboard[slot].ay[pair]);
            m->mockingboard[slot].ay_pending_cycles[pair] = r_u32(&r);
            m->mockingboard[slot].ay_bus_state[pair] = r_u8(&r);
        }
        m->mockingboard[slot].board_startup_timer_seed_disabled = r_u8(&r);
        if (!r.ok) {
            return false;
        }
    }
    return r.ok;
}

bool apple2_snapshot_load(apple2_t *m, const uint8_t *in, size_t in_len)
{
    parsed_chunks chunks;

    if (m == NULL || in == NULL || m->ram_main == NULL || m->ram_lc == NULL) {
        return false;
    }
    if (!parse_chunks(in, in_len, &chunks)) {
        return false;
    }

    apple2_paste_cancel(m);
    if (m->write_history != NULL) {
        memset(m->write_history, 0, APPLE2_ADDR_SPACE * sizeof(uint64_t));
    }

    if (!apply_meta(m, chunks.meta, chunks.meta_len) ||
        !apply_slot(m, chunks.slot, chunks.slot_len) ||
        !apply_ram(m, chunks.ram, chunks.ram_len) ||
        !apply_soft(m, chunks.soft, chunks.soft_len) ||
        !apply_cpu(m, chunks.cpu, chunks.cpu_len) ||
        !apply_vid(m, chunks.vid, chunks.vid_len) ||
        !apply_dsks(m, chunks.dsks, chunks.dsks_len) ||
        !apply_sprt(m, chunks.sprt, chunks.sprt_len) ||
        !apply_mbrd(m, chunks.mbrd, chunks.mbrd_len)) {
        return false;
    }

    softswitch_apply_full_map(m);
    if (m->video.fb != NULL) {
        apple2_video_paint_full_frame(m);
    }
    return true;
}

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

#define DSK_TRACKS             35
#define DSK_SECTORS            16
#define DSK_SECTOR_SIZE        256
#define DSK_FILE_SIZE          (DSK_TRACKS * DSK_SECTORS * DSK_SECTOR_SIZE)
#define DSK_NIB_TRACK_SIZE     6656
#define DSK_VOLUME             254

static const uint8_t dsk_write_translate[64] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

static const uint8_t dsk_dos33_sector_map[DSK_SECTORS] = {
    0x0, 0x7, 0xe, 0x6, 0xd, 0x5, 0xc, 0x4,
    0xb, 0x3, 0xa, 0x2, 0x9, 0x1, 0x8, 0xf,
};

static const uint8_t dsk_prodos_sector_map[DSK_SECTORS] = {
    0x0, 0x8, 0x1, 0x9, 0x2, 0xa, 0x3, 0xb,
    0x4, 0xc, 0x5, 0xd, 0x6, 0xe, 0x7, 0xf,
};

static const uint8_t dsk_flip2[4] = {0, 2, 1, 3};
static const uint8_t dsk_flip4[4] = {0, 8, 4, 12};
static const uint8_t dsk_flip6[4] = {0, 32, 16, 48};
static const uint8_t dsk_unflip2[4] = {0, 2, 1, 3};

static void image_emit_byte(uint8_t **out, uint8_t byte) {
    *(*out)++ = byte;
}

static void image_emit_repeat(uint8_t **out, uint8_t byte, size_t count) {
    memset(*out, byte, count);
    *out += count;
}

static void image_emit_44(uint8_t **out, uint8_t byte) {
    image_emit_byte(out, (uint8_t)((byte >> 1) | 0xaa));
    image_emit_byte(out, (uint8_t)(byte | 0xaa));
}

static int image_init_nib_specifics(DISKII_IMAGE *image, uint32_t track_size, uint32_t num_tracks, uint32_t writable, uint32_t file_backed) {
    image->image_specifics = malloc(sizeof(IMAGE_NIB));
    if(!image->image_specifics) {
        return A2_ERR;
    }
    memset(image->image_specifics, 0, sizeof(IMAGE_NIB));

    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    nib->dirty_tracks = calloc(num_tracks, sizeof(uint8_t));
    if(!nib->dirty_tracks) {
        free(image->image_specifics);
        image->image_specifics = NULL;
        return A2_ERR;
    }
    nib->writable = writable;
    nib->file_backed = file_backed;
    nib->track_size = track_size;
    nib->num_tracks = num_tracks;
    nib->sector_order = DSK_SECTOR_ORDER_OTHER;
    return A2_OK;
}

static int image_dsk_translate_index(uint8_t byte) {
    for(int i = 0; i < 64; i++) {
        if(dsk_write_translate[i] == byte) {
            return i;
        }
    }
    return -1;
}

static uint8_t image_decode_44(uint8_t a, uint8_t b) {
    return (uint8_t)(((a << 1) & 0xaa) | (b & 0x55));
}

static void image_dsk_emit_sector(uint8_t **out, uint8_t volume, uint8_t track, uint8_t sector, const uint8_t *data) {
    uint8_t checksum;
    uint8_t last = 0;

    image_emit_repeat(out, 0xff, 22);

    image_emit_byte(out, 0xd5);
    image_emit_byte(out, 0xaa);
    image_emit_byte(out, 0x96);
    image_emit_44(out, volume);
    image_emit_44(out, track);
    image_emit_44(out, sector);
    image_emit_44(out, (uint8_t)(volume ^ track ^ sector));
    image_emit_byte(out, 0xde);
    image_emit_byte(out, 0xaa);
    image_emit_byte(out, 0xeb);

    image_emit_repeat(out, 0xff, 5);

    image_emit_byte(out, 0xd5);
    image_emit_byte(out, 0xaa);
    image_emit_byte(out, 0xad);

    for(int i = 0; i < 86; i++) {
        uint8_t b0 = data[i];
        uint8_t b1 = (i + 86 < DSK_SECTOR_SIZE) ? data[i + 86] : 0;
        uint8_t b2 = (i + 172 < DSK_SECTOR_SIZE) ? data[i + 172] : 0;
        uint8_t val = (uint8_t)(dsk_flip2[b0 & 3] | dsk_flip4[b1 & 3] | dsk_flip6[b2 & 3]);
        image_emit_byte(out, dsk_write_translate[(last ^ val) & 0x3f]);
        last = val;
    }

    for(int i = 0; i < DSK_SECTOR_SIZE; i++) {
        uint8_t val = (uint8_t)(data[i] >> 2);
        image_emit_byte(out, dsk_write_translate[(last ^ val) & 0x3f]);
        last = val;
    }

    checksum = last;
    image_emit_byte(out, dsk_write_translate[checksum & 0x3f]);
    image_emit_byte(out, 0xde);
    image_emit_byte(out, 0xaa);
    image_emit_byte(out, 0xeb);
    image_emit_repeat(out, 0xff, 26);
}

static int image_dsk_decode_sector(const uint8_t *track_data, uint32_t data_pos, uint8_t *sector_data) {
    uint8_t values[DSK_SECTOR_SIZE + 86];
    uint8_t last = 0;

    for(int i = 0; i < DSK_SECTOR_SIZE + 86; i++) {
        int translated = image_dsk_translate_index(track_data[(data_pos + 3 + i) % DSK_NIB_TRACK_SIZE]);
        if(translated < 0) {
            return A2_ERR;
        }
        values[i] = (uint8_t)(translated ^ last);
        last = values[i];
    }

    int translated_checksum = image_dsk_translate_index(track_data[(data_pos + 3 + DSK_SECTOR_SIZE + 86) % DSK_NIB_TRACK_SIZE]);
    if(translated_checksum < 0 || (uint8_t)translated_checksum != last) {
        return A2_ERR;
    }

    memset(sector_data, 0, DSK_SECTOR_SIZE);
    for(int i = 0; i < DSK_SECTOR_SIZE; i++) {
        sector_data[i] = (uint8_t)(values[86 + i] << 2);
    }

    for(int i = 0; i < 86; i++) {
        uint8_t low_bits = values[i];
        sector_data[i] |= dsk_unflip2[low_bits & 0x03];
        if(i + 86 < DSK_SECTOR_SIZE) {
            sector_data[i + 86] |= dsk_unflip2[(low_bits >> 2) & 0x03];
        }
        if(i + 172 < DSK_SECTOR_SIZE) {
            sector_data[i + 172] |= dsk_unflip2[(low_bits >> 4) & 0x03];
        }
    }

    return A2_OK;
}

static int image_dsk_decode_track(DISKII_IMAGE *image, IMAGE_NIB *nib, uint32_t track, uint8_t *sectors) {
    if(!image || !image->file.file_data || !nib || !sectors || track >= nib->num_tracks || nib->track_size != DSK_NIB_TRACK_SIZE) {
        return A2_ERR;
    }

    uint8_t *track_data = (uint8_t *)&image->file.file_data[track * nib->track_size];
    uint8_t seen[DSK_SECTORS];
    memset(seen, 0, sizeof(seen));
    memset(sectors, 0, DSK_SECTORS * DSK_SECTOR_SIZE);

    for(uint32_t pos = 0; pos < nib->track_size; pos++) {
        if(track_data[pos] != 0xd5 ||
                track_data[(pos + 1) % nib->track_size] != 0xaa ||
                track_data[(pos + 2) % nib->track_size] != 0x96) {
            continue;
        }

        uint8_t decoded_track = image_decode_44(track_data[(pos + 5) % nib->track_size], track_data[(pos + 6) % nib->track_size]);
        uint8_t sector = image_decode_44(track_data[(pos + 7) % nib->track_size], track_data[(pos + 8) % nib->track_size]);
        uint8_t checksum = image_decode_44(track_data[(pos + 9) % nib->track_size], track_data[(pos + 10) % nib->track_size]);
        if(sector >= DSK_SECTORS || decoded_track != (uint8_t)track ||
                ((image_decode_44(track_data[(pos + 3) % nib->track_size], track_data[(pos + 4) % nib->track_size]) ^ decoded_track ^ sector) & 0xff) != checksum) {
            continue;
        }

        uint32_t data_pos = UINT32_MAX;
        for(uint32_t scan = pos + 11; scan < pos + 128; scan++) {
            uint32_t p = scan % nib->track_size;
            if(track_data[p] == 0xd5 &&
                    track_data[(p + 1) % nib->track_size] == 0xaa &&
                    track_data[(p + 2) % nib->track_size] == 0xad) {
                data_pos = p;
                break;
            }
        }
        if(data_pos == UINT32_MAX) {
            continue;
        }

        uint8_t decoded_sector[DSK_SECTOR_SIZE];
        if(A2_OK != image_dsk_decode_sector(track_data, data_pos, decoded_sector)) {
            continue;
        }
        memcpy(&sectors[sector * DSK_SECTOR_SIZE], decoded_sector, DSK_SECTOR_SIZE);
        seen[sector] = 1;
    }

    for(int sector = 0; sector < DSK_SECTORS; sector++) {
        if(!seen[sector]) {
            return A2_ERR;
        }
    }
    return A2_OK;
}

double image_cycles_per_byte(DISKII_IMAGE *image) {
    if(!image || !image->image_specifics) {
        return 32.0;
    }

    switch(image->kind) {
        case IMG_NIB: {
                return 32.0;
            }
            break;

        case IMG_DSK:
        case IMG_WOZ:
            break;
    }

    return 32.0;
}

static uint64_t image_advance_bytes(APPLE2 *m, DISKII_DRIVE *d, DISKII_IMAGE *image) {
    double delta = (double)m->cpu.cycles - d->q6_last_read_cycles;
    double cycles_per_byte = image_cycles_per_byte(image);
    uint64_t nbytes = delta > 0.0 ? (uint64_t)(delta / cycles_per_byte) : 0;

    if(!nbytes) {
        return 0;
    }

    switch(image->kind) {
        case IMG_NIB: {
                IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
                nib->track_read_pos = (nib->track_read_pos + nbytes) % nib->track_size;
            }
            break;

        case IMG_DSK:
        case IMG_WOZ:
            break;
    }

    d->q6_last_read_cycles += (double)nbytes * cycles_per_byte;
    return nbytes;
}

uint8_t image_get_byte(APPLE2 *m, DISKII_DRIVE *d) {
    // static int pos = 0;
    // active image is set at this point
    DISKII_IMAGE *image = d->active_image;
    uint8_t byte = rand() & 0x7f;

    switch(image->kind) {
        case IMG_DSK: {
            }
            break;

        case IMG_NIB: {
                IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
                image_advance_bytes(m, d, image);
                byte = image->file.file_data[nib->track_index_pos + nib->track_read_pos];
            }
            break;

        case IMG_WOZ: {
            }
            break;
    }
    return byte;
}

int image_put_byte(APPLE2 *m, DISKII_DRIVE *d, uint8_t byte) {
    DISKII_IMAGE *image = d->active_image;
    if(!image || !image->image_specifics) {
        return A2_ERR;
    }

    switch(image->kind) {
        case IMG_NIB: {
                IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
                if(!nib->writable || !nib->track_size || !nib->num_tracks) {
                    return A2_ERR;
                }

                if(!d->write_active) {
                    image_advance_bytes(m, d, image);
                }

                uint32_t track = nib->track_index_pos / nib->track_size;
                if(track >= nib->num_tracks || nib->track_read_pos >= nib->track_size) {
                    return A2_ERR;
                }

                uint32_t pos = nib->track_index_pos + nib->track_read_pos;
                if(pos >= (uint32_t)image->file.file_size) {
                    return A2_ERR;
                }

                if(!d->write_active) {
                    d->write_track = track;
                    d->write_start_pos = nib->track_read_pos;
                    d->write_byte_count = 0;
                }

                image->file.file_data[pos] = (char)byte;
                nib->dirty_tracks[track] = 1;
                nib->dirty = 1;
                nib->track_read_pos = (nib->track_read_pos + 1) % nib->track_size;
                d->q6_last_read_cycles = (double)m->cpu.cycles;
                d->write_active = 1;
                d->write_byte_count++;
                return A2_OK;
            }

        case IMG_DSK:
        case IMG_WOZ:
            break;
    }

    return A2_ERR;
}

int image_finish_write(DISKII_DRIVE *d) {
    if(!d || !d->write_active || !d->active_image || d->active_image->kind != IMG_NIB || !d->active_image->image_specifics) {
        return A2_OK;
    }

    DISKII_IMAGE *image = d->active_image;
    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    if(!nib->track_size || d->write_track >= nib->num_tracks || d->write_byte_count < (nib->track_size / 2) ||
            d->write_byte_count >= nib->track_size) {
        return A2_OK;
    }

    uint32_t current_pos = nib->track_read_pos;
    uint32_t pos = current_pos;
    while(pos != d->write_start_pos) {
        uint32_t file_pos = (d->write_track * nib->track_size) + pos;
        if(file_pos >= (uint32_t)image->file.file_size) {
            return A2_ERR;
        }
        image->file.file_data[file_pos] = (char)0xff;
        pos = (pos + 1) % nib->track_size;
    }

    nib->dirty_tracks[d->write_track] = 1;
    nib->dirty = 1;
    return A2_OK;
}

int image_is_dirty(DISKII_IMAGE *image) {
    if(!image || image->kind != IMG_NIB || !image->image_specifics) {
        return 0;
    }

    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    return nib->dirty ? 1 : 0;
}

int image_save(DISKII_IMAGE *image) {
    if(!image || image->kind != IMG_NIB || !image->image_specifics) {
        return A2_ERR;
    }

    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    if(!nib->dirty) {
        return A2_OK;
    }
    if((!nib->file_backed && !nib->dsk_backed) || !image->file.file_path || !image->file.file_data || image->file.file_size < 0) {
        return A2_ERR;
    }

    UTIL_FILE out;
    memset(&out, 0, sizeof(out));
    if(A2_OK != util_file_open(&out, image->file.file_path, "rb+")) {
        return A2_ERR;
    }

    int failed = 0;
    if(nib->dsk_backed) {
        if(out.file_size != DSK_FILE_SIZE || nib->num_tracks != DSK_TRACKS) {
            failed = 1;
        }
        const uint8_t *sector_map = nib->sector_order == DSK_SECTOR_ORDER_PRODOS ? dsk_prodos_sector_map : dsk_dos33_sector_map;
        uint8_t sectors[DSK_SECTORS * DSK_SECTOR_SIZE];
        for(uint32_t track = 0; track < nib->num_tracks && !failed; track++) {
            if(!nib->dirty_tracks[track]) {
                continue;
            }
            if(A2_OK != image_dsk_decode_track(image, nib, track, sectors)) {
                failed = 1;
                break;
            }
            for(uint32_t sector = 0; sector < DSK_SECTORS; sector++) {
                uint32_t file_sector = sector_map[sector];
                long offset = (long)(((track * DSK_SECTORS) + file_sector) * DSK_SECTOR_SIZE);
                if(fseek(out.fp, offset, SEEK_SET) != 0 ||
                        fwrite(&sectors[sector * DSK_SECTOR_SIZE], 1, DSK_SECTOR_SIZE, out.fp) != DSK_SECTOR_SIZE) {
                    failed = 1;
                    break;
                }
            }
        }
    } else {
        for(uint32_t track = 0; track < nib->num_tracks && !failed; track++) {
            if(!nib->dirty_tracks[track]) {
                continue;
            }

            int64_t offset = (int64_t)track * (int64_t)nib->track_size;
            if(offset < 0 || offset + (int64_t)nib->track_size > image->file.file_size ||
                    fseek(out.fp, (long)offset, SEEK_SET) != 0) {
                failed = 1;
                break;
            }

            size_t wrote = fwrite(&image->file.file_data[offset], 1, nib->track_size, out.fp);
            if(wrote != nib->track_size) {
                failed = 1;
            }
        }
    }

    if(fflush(out.fp) != 0 || ferror(out.fp)) {
        failed = 1;
    }
    util_file_discard(&out);
    if(failed) {
        return A2_ERR;
    }

    memset(nib->dirty_tracks, 0, nib->num_tracks * sizeof(uint8_t));
    nib->dirty = 0;
    return A2_OK;
}

void image_head_position(DISKII_IMAGE *image, uint32_t quater_track) {
    if(!image->image_specifics) {
        return;
    }
    switch(image->kind) {
        case IMG_DSK: {
            }
            break;

        case IMG_NIB: {
                IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
                nib->track_index_pos = (quater_track / 4);
                if(nib->track_index_pos >= nib->num_tracks) {
                    nib->track_index_pos = nib->num_tracks - 1;
                }
                nib->track_index_pos *= nib->track_size;
            }
            break;

        case IMG_WOZ: {
            }
            break;
    }
}

int image_load_dsk(APPLE2 *m, DISKII_IMAGE *image, const char *ext) {
    UNUSED(m);
    if(image->file.file_size != DSK_FILE_SIZE) {
        return A2_ERR;
    }

    DISKII_SECTOR_ORDER order = DSK_SECTOR_ORDER_DOS33;
    if(ext && 0 == stricmp(ext, ".po")) {
        order = DSK_SECTOR_ORDER_PRODOS;
    }

    const uint8_t *sector_map = order == DSK_SECTOR_ORDER_PRODOS ? dsk_prodos_sector_map : dsk_dos33_sector_map;
    uint8_t *sector_data = (uint8_t *)image->file.file_data;
    uint8_t *raw_tracks = malloc(DSK_TRACKS * DSK_NIB_TRACK_SIZE);
    if(!raw_tracks) {
        return A2_ERR;
    }

    for(int track = 0; track < DSK_TRACKS; track++) {
        uint8_t *out = raw_tracks + (track * DSK_NIB_TRACK_SIZE);
        uint8_t *track_end = out + DSK_NIB_TRACK_SIZE;

        for(int sector = 0; sector < DSK_SECTORS; sector++) {
            int file_sector = sector_map[sector];
            const uint8_t *source = sector_data + (((track * DSK_SECTORS) + file_sector) * DSK_SECTOR_SIZE);
            image_dsk_emit_sector(&out, DSK_VOLUME, (uint8_t)track, (uint8_t)sector, source);
        }

        if(out > track_end) {
            free(raw_tracks);
            return A2_ERR;
        }
        image_emit_repeat(&out, 0xff, (size_t)(track_end - out));
    }

    free(image->file.file_data);
    image->file.file_data = (char *)raw_tracks;
    image->file.file_size = DSK_TRACKS * DSK_NIB_TRACK_SIZE;

    image->kind = IMG_NIB;
    image->disk_encoding = DSK_ENCODING_16SECTOR;
    if(A2_OK != image_init_nib_specifics(image, DSK_NIB_TRACK_SIZE, DSK_TRACKS, 1, 0)) {
        return A2_ERR;
    }
    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    nib->dsk_backed = 1;
    nib->sector_order = order;
    return A2_OK;
}

int image_load_nib(APPLE2 *m, DISKII_IMAGE *image) {
    uint32_t track_size = 6384;
    image->kind = IMG_NIB;

    if(!(image->file.file_size % 6656)) {
        track_size = 6656;
    }
    if(image->file.file_size % track_size) {
        return A2_ERR;
    }

    int rom_count[2] = {0, 0};
    if(A2_OK != image_init_nib_specifics(image, track_size, image->file.file_size / track_size, 1, 1)) {
        return A2_ERR;
    }

    for(int i = 0; i < image->file.file_size - 2; i++) {
        if((uint8_t)image->file.file_data[i] == 0xD5 && (uint8_t)image->file.file_data[i + 1] == 0xAA) {
            if((uint8_t)image->file.file_data[i + 2] == 0xB5) {
                rom_count[DSK_ENCODING_13SECTOR]++;
            } else if((uint8_t)image->file.file_data[i + 2] == 0x96) {
                rom_count[DSK_ENCODING_16SECTOR]++;
            } else {
                i += 1;
            }
        }
    }
    if(rom_count[DSK_ENCODING_13SECTOR] > rom_count[DSK_ENCODING_16SECTOR]) {
        image->disk_encoding = DSK_ENCODING_13SECTOR;
    } else if(rom_count[DSK_ENCODING_16SECTOR]) {
        image->disk_encoding = DSK_ENCODING_16SECTOR;
    } else {
        return A2_ERR;
    }

    return A2_OK;
}

int image_load_woz(APPLE2 *m, DISKII_IMAGE *image) {
    return A2_ERR;
}

void image_shutdown(DISKII_IMAGE *image) {
    if(image->image_specifics) {
        image_save(image);
        if(image->kind == IMG_NIB) {
            IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
            free(nib->dirty_tracks);
            nib->dirty_tracks = NULL;
        }
        free(image->image_specifics);
        image->image_specifics = NULL;
    }
    util_file_discard(&image->file);
}

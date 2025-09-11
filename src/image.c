// Apple ][+ emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"

int image_load_dsk(APPLE2 *m, diskii_image_t *image) {
    return A2_ERR;
}

int image_load_nib(APPLE2 *m, diskii_image_t *image) {
    uint32_t track_size;
    image->kind = IMG_NIB;

    if(!(image->file.file_size % 6656)) {
        track_size = 6656;
    } else {
        track_size = 6384;
    }
    if(image->file.file_size % track_size) {
        return A2_ERR;
    }

    image->image_specifics = malloc(sizeof(image_nib_t));
    if(!image->image_specifics) {
        return A2_ERR;
    }
    memset(image->image_specifics, 0, sizeof(image_nib_t));
    image_nib_t *nib = (image_nib_t *)image->image_specifics;
    nib->writable = 1;
    nib->track_size = track_size;
    nib->num_tracks = image->file.file_size / track_size;
    return A2_OK;
}

int image_load_woz(APPLE2 *m, diskii_image_t *image) {
    return A2_ERR;
}

void image_head_position(diskii_image_t *image, uint32_t quater_track) {
    if(!image->image_specifics) {
        return;
    }
    switch(image->kind) {
        case IMG_DSK: {
            }
            break;

        case IMG_NIB: {
                image_nib_t *nib = (image_nib_t *)image->image_specifics;
                nib->track_index_pos = (quater_track / 4);
                if(nib->track_index_pos > nib->num_tracks) {
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

uint8_t image_get_byte(APPLE2 *m, diskii_drive_t *d) {
    // static int pos = 0;
    diskii_image_t *image = &d->image;
    uint8_t byte = rand() & 0x7f;

    switch(image->kind) {
        case IMG_DSK: {
            }
            break;

        case IMG_NIB: {
                image_nib_t *nib = (image_nib_t *)image->image_specifics;
                uint64_t delta   = m->cpu.cycles - d->q6_last_read_cycles;
                uint64_t nbytes  = delta / 32;
                if(nbytes) {
                    nib->track_read_pos = (nib->track_read_pos + nbytes) % nib->track_size;
                    d->q6_last_read_cycles += nbytes * 32;
                }
                byte = image->file.file_data[nib->track_index_pos + nib->track_read_pos];
                // if(!(pos++ % 16)) { printf("\n%04X: ", nib->track_index_pos + nib->track_read_pos);}
                // printf("%02X ", byte);
            }
            break;

        case IMG_WOZ: {
            }
            break;
    }
    return byte;
}

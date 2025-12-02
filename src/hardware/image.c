// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

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
                // How many cpu cycles since last read
                uint64_t delta   = m->cpu.cycles - d->q6_last_read_cycles;
                // Advance by as many bytes as would pass in 32 cpu cycles
                uint64_t nbytes  = delta / 32;
                if(nbytes) {
                    nib->track_read_pos = (nib->track_read_pos + nbytes) % nib->track_size;
                    // The byte was read at a 32 cycle increment (not at m->cpu.cycles)
                    d->q6_last_read_cycles += nbytes * 32;
                }
                byte = image->file.file_data[nib->track_index_pos + nib->track_read_pos];
            }
            break;

        case IMG_WOZ: {
            }
            break;
    }
    return byte;
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

int image_load_dsk(APPLE2 *m, DISKII_IMAGE *image, const char *ext) {

    return A2_ERR;
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

    image->image_specifics = malloc(sizeof(IMAGE_NIB));
    if(!image->image_specifics) {
        return A2_ERR;
    }
    memset(image->image_specifics, 0, sizeof(IMAGE_NIB));
    int rom_count[2] = {0, 0};
    IMAGE_NIB *nib = (IMAGE_NIB *)image->image_specifics;
    nib->writable = 1;
    nib->track_size = track_size;
    nib->num_tracks = image->file.file_size / track_size;

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
    util_file_discard(&image->file);
    if(image->image_specifics) {
        // No specific image type currently has internal allocations so just
        // ditch the specific image
        free(image->image_specifics);
        image->image_specifics = NULL;
    }
}


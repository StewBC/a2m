// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

int sp_mount(APPLE2 *m, const int slot, const int device, const char *file_name) {
    SP_DEVICE *spd = &m->sp_device[slot];
    if(m->slot_cards[slot].slot_type != SLOT_TYPE_SMARTPORT) {
        return A2_ERR;
    }

    UTIL_FILE *f = &spd->sp_files[device];
    if(A2_OK != util_file_open(f, file_name, "rb+")) {
        return A2_ERR;
    }
    // Force a block 0 read on the correct device
    spd->sp_buffer[1] = device << 7;
    spd->sp_buffer[2] = spd->sp_buffer[3] = 0;
    sp_read(m, slot);
    if(spd->sp_buffer[0] != SP_SUCCESS) {
        util_file_close(f);
    }

    if(strncmp((char *) &m->sp_device[slot].sp_buffer[1], "2IMG", 4) == 0) {
        spd->file_header_size[device] = 0x40;
        f->file_size -= 0x40;
    } else {
        spd->file_header_size[device] = 0x00;
    }

    return A2_OK;
}

void sp_read(APPLE2 *m, const int slot) {
    SP_DEVICE *spd = &m->sp_device[slot];
    int device = spd->sp_buffer[1] >> 7;
    uint16_t block = *(uint16_t *) & spd->sp_buffer[2];
    UTIL_FILE *f = &spd->sp_files[device];

    if(!f->is_file_open || block > f->file_size / SP_BLOCK_SIZE) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }
    if(fseek(f->fp, spd->file_header_size[device] + (block * SP_BLOCK_SIZE), SEEK_SET) != 0) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    size_t bread = fread(&(spd->sp_buffer[1]), 1, SP_BLOCK_SIZE, spd->sp_files[device].fp);
    if(SP_BLOCK_SIZE != bread) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }
    if(m->a2out_cb.cb_diskactivity_ctx.cb_diskread) {
        m->a2out_cb.cb_diskactivity_ctx.cb_diskread(m->a2out_cb.cb_diskactivity_ctx.user);
    }
    spd->sp_buffer[0] = SP_SUCCESS;
}

void sp_status(APPLE2 *m, const int slot) {
    int device = m->sp_device[slot].sp_buffer[1] >> 7;
    SP_DEVICE *spd = &m->sp_device[slot];
    UTIL_FILE *f = &spd->sp_files[device];
    uint16_t blocks = f->file_size / SP_BLOCK_SIZE;

    if(!blocks) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    spd->sp_buffer[0] = SP_SUCCESS;
    spd->sp_buffer[1] = blocks % 0x100;
    spd->sp_buffer[2] = blocks / 0x100;
}

void sp_write(APPLE2 *m, const int slot) {
    int device = m->sp_device[slot].sp_buffer[1] >> 7;
    SP_DEVICE *spd = &m->sp_device[slot];
    uint16_t block = *(uint16_t *) & spd->sp_buffer[2];
    UTIL_FILE *f = &spd->sp_files[device];
    const uint8_t *data = (uint8_t *) & m->sp_device[slot].sp_buffer[4];

    if(!(f->is_file_open && block < spd->sp_files[device].file_size / SP_BLOCK_SIZE &&
            fseek(spd->sp_files[device].fp, spd->file_header_size[device] + (block * SP_BLOCK_SIZE), SEEK_SET) == 0)) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    if(SP_BLOCK_SIZE != fwrite(data, 1, SP_BLOCK_SIZE, spd->sp_files[device].fp)) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    if(m->a2out_cb.cb_diskactivity_ctx.cb_diskwrite) {
        m->a2out_cb.cb_diskactivity_ctx.cb_diskwrite(m->a2out_cb.cb_diskactivity_ctx.user);
    }
    spd->sp_buffer[0] = SP_SUCCESS;
}

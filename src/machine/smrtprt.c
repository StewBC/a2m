// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "a2_status.h"
#include "apple2.h"
#include "hostfs.h"
#include "periph_lib.h"
#include "smrtprt.h"

#include <stdio.h>
#include <string.h>

/* SmartPort protocol command numbers (standard, non-extended). */
enum {
    SP_CMD_STATUS = 0x00,
    SP_CMD_READ_BLOCK = 0x01,
    SP_CMD_WRITE_BLOCK = 0x02
};

/* Known pure-SP entry points referenced by our Cn slot ROM. */
enum {
    SP_ENTRY_C800 = 0xC800u,
    SP_ENTRY_C89B = 0xC89Bu,
    SP_ENTRY_C9AA = 0xC9AAu
};

bool sp_unit_mounted(const SP_DEVICE *spd, int device)
{
    if (spd == NULL || device < 0 || device > 1) {
        return false;
    }
    if (spd->backend[device] == SP_BACKEND_HOSTFS) {
        return spd->hostfs[device] != NULL;
    }
    return spd->sp_files[device].is_file_open != 0;
}

const char *sp_unit_path(const SP_DEVICE *spd, int device)
{
    if (spd == NULL || device < 0 || device > 1) {
        return NULL;
    }
    if (spd->backend[device] == SP_BACKEND_HOSTFS && spd->hostfs[device] != NULL) {
        return hostfs_root_path(spd->hostfs[device]);
    }
    if (spd->sp_files[device].is_used) {
        return spd->sp_files[device].file_path;
    }
    return NULL;
}

const char *sp_unit_display_name(const SP_DEVICE *spd, int device)
{
    const char *path;
    const char *slash;

    if (spd == NULL || device < 0 || device > 1) {
        return NULL;
    }
    if (spd->backend[device] == SP_BACKEND_IMAGE &&
        spd->sp_files[device].file_display_name != NULL) {
        return spd->sp_files[device].file_display_name;
    }
    path = sp_unit_path(spd, device);
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    slash = strrchr(path, '/');
#ifdef _WIN32
    {
        const char *b = strrchr(path, '\\');
        if (b != NULL && (slash == NULL || b > slash)) {
            slash = b;
        }
    }
#endif
    return slash != NULL ? slash + 1 : path;
}

int sp_mount(apple2_t *m, int slot, int device, const char *file_name) {
    char vol_name[16];

    if(m == NULL || slot < 1 || slot > 7 || device < 0 || device > 1 ||
            file_name == NULL || file_name[0] == '\0') {
        return A2_ERR;
    }
    SP_DEVICE *spd = &m->sp_device[slot];
    if(m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
        return A2_ERR;
    }

    if(sp_unit_mounted(spd, device)) {
        if(sp_eject(m, slot, device) != A2_OK) {
            return A2_ERR;
        }
    }

    if(hostfs_path_is_dir(file_name)) {
        hostfs_volume *vol;

        snprintf(vol_name, sizeof(vol_name), "HOSTFS.S%uD%u",
            (unsigned)slot, (unsigned)device);
        vol = hostfs_mount(file_name, vol_name);
        if(vol == NULL) {
            return A2_ERR;
        }
        spd->backend[device] = SP_BACKEND_HOSTFS;
        spd->hostfs[device] = vol;
        spd->file_header_size[device] = 0;
        hostfs_bind_apple(vol, m, slot, device);

        spd->sp_buffer[1] = (uint8_t)(device << 7);
        spd->sp_buffer[2] = spd->sp_buffer[3] = 0;
        sp_read(m, slot);
        if(spd->sp_buffer[0] != SP_SUCCESS) {
            hostfs_eject(vol);
            spd->hostfs[device] = NULL;
            spd->backend[device] = SP_BACKEND_NONE;
            return A2_ERR;
        }
        return A2_OK;
    }

    UTIL_FILE *f = &spd->sp_files[device];
    if(A2_OK != util_file_open(f, file_name, "rb+")) {
        return A2_ERR;
    }
    spd->backend[device] = SP_BACKEND_IMAGE;
    // Force a block 0 read on the correct device
    spd->sp_buffer[1] = (uint8_t)(device << 7);
    spd->sp_buffer[2] = spd->sp_buffer[3] = 0;
    sp_read(m, slot);
    if(spd->sp_buffer[0] != SP_SUCCESS) {
        util_file_discard(f);
        spd->backend[device] = SP_BACKEND_NONE;
        return A2_ERR;
    }

    if(strncmp((char *) &m->sp_device[slot].sp_buffer[1], "2IMG", 4) == 0) {
        spd->file_header_size[device] = m->sp_device[slot].sp_buffer[9] + m->sp_device[slot].sp_buffer[10] * 256;
        f->file_size -= spd->file_header_size[device];
    } else {
        spd->file_header_size[device] = 0x00;
    }

    return A2_OK;
}

int sp_eject(apple2_t *m, int slot, int device) {
    UTIL_FILE *f;
    SP_DEVICE *spd;
    if(m == NULL || slot < 1 || slot > 7 || device < 0 || device > 1 ||
            m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
        return A2_ERR;
    }
    spd = &m->sp_device[slot];
    if(spd->backend[device] == SP_BACKEND_HOSTFS) {
        hostfs_eject(spd->hostfs[device]);
        spd->hostfs[device] = NULL;
        spd->backend[device] = SP_BACKEND_NONE;
        spd->file_header_size[device] = 0;
        return A2_OK;
    }
    f = &spd->sp_files[device];
    if(f->is_file_open && fflush(f->fp) != 0) {
        return A2_ERR;
    }
    util_file_discard(f);
    spd->file_header_size[device] = 0;
    spd->backend[device] = SP_BACKEND_NONE;
    return A2_OK;
}

int sp_flush_all(apple2_t *m) {
    int slot;
    int device;
    if(m == NULL) {
        return A2_ERR;
    }
    for(slot = 1; slot <= 7; ++slot) {
        if(m->slot_type[slot] != SLOT_TYPE_SMARTPORT) {
            continue;
        }
        for(device = 0; device < 2; ++device) {
            UTIL_FILE *f;
            if(m->sp_device[slot].backend[device] == SP_BACKEND_HOSTFS) {
                if(hostfs_flush(m->sp_device[slot].hostfs[device]) != A2_OK) {
                    return A2_ERR;
                }
                continue;
            }
            f = &m->sp_device[slot].sp_files[device];
            if(f->is_file_open && fflush(f->fp) != 0) {
                return A2_ERR;
            }
        }
    }
    return A2_OK;
}

void sp_read(apple2_t *m, int slot) {
    SP_DEVICE *spd = &m->sp_device[slot];
    int device = spd->sp_buffer[1] >> 7;
    uint16_t block = *(uint16_t *) & spd->sp_buffer[2];

    if(device < 0 || device > 1) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    if(spd->backend[device] == SP_BACKEND_HOSTFS) {
        hostfs_volume *vol = spd->hostfs[device];
        if(vol == NULL) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }
        hostfs_maybe_refresh(vol);
        if(hostfs_read_block(vol, block, &spd->sp_buffer[1]) != A2_OK) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }
        spd->sp_buffer[0] = SP_SUCCESS;
        return;
    }

    {
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
        spd->sp_buffer[0] = SP_SUCCESS;
    }
}

void sp_status(apple2_t *m, int slot) {
    int device = m->sp_device[slot].sp_buffer[1] >> 7;
    SP_DEVICE *spd = &m->sp_device[slot];
    uint16_t blocks;

    if(device < 0 || device > 1 || !sp_unit_mounted(spd, device)) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    if(spd->backend[device] == SP_BACKEND_HOSTFS) {
        hostfs_maybe_refresh(spd->hostfs[device]);
        blocks = hostfs_total_blocks(spd->hostfs[device]);
    } else {
        blocks = (uint16_t)(spd->sp_files[device].file_size / SP_BLOCK_SIZE);
    }

    if(!blocks) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    spd->sp_buffer[0] = SP_SUCCESS;
    spd->sp_buffer[1] = (uint8_t)(blocks % 0x100);
    spd->sp_buffer[2] = (uint8_t)(blocks / 0x100);
}

void sp_write(apple2_t *m, int slot) {
    int device = m->sp_device[slot].sp_buffer[1] >> 7;
    SP_DEVICE *spd = &m->sp_device[slot];
    uint16_t block = *(uint16_t *) & spd->sp_buffer[2];
    const uint8_t *data = (uint8_t *) & m->sp_device[slot].sp_buffer[4];

    if(device < 0 || device > 1) {
        spd->sp_buffer[0] = SP_IO_ERROR;
        return;
    }

    if(spd->backend[device] == SP_BACKEND_HOSTFS) {
        if(spd->hostfs[device] == NULL) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }
        hostfs_maybe_refresh(spd->hostfs[device]);
        if(hostfs_write_block(spd->hostfs[device], block, data) != A2_OK) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }
        spd->sp_buffer[0] = SP_SUCCESS;
        return;
    }

    {
        UTIL_FILE *f = &spd->sp_files[device];

        if(!(f->is_file_open && block < spd->sp_files[device].file_size / SP_BLOCK_SIZE &&
                fseek(spd->sp_files[device].fp, spd->file_header_size[device] + (block * SP_BLOCK_SIZE), SEEK_SET) == 0)) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }

        if(SP_BLOCK_SIZE != fwrite(data, 1, SP_BLOCK_SIZE, spd->sp_files[device].fp)) {
            spd->sp_buffer[0] = SP_IO_ERROR;
            return;
        }
        spd->sp_buffer[0] = SP_SUCCESS;
    }
}

void sp_shutdown(apple2_t *m) {
    (void)sp_flush_all(m);
    for(int slot = 1; slot <= 7; slot++) {
        if(m->slot_type[slot] == SLOT_TYPE_SMARTPORT) {
            (void)sp_eject(m, slot, 0);
            (void)sp_eject(m, slot, 1);
        }
    }
}

static uint8_t sp_cpu_read(apple2_t *m, uint16_t addr)
{
    return apple2_debug_read(m, addr);
}

static void sp_cpu_write(apple2_t *m, uint16_t addr, uint8_t value)
{
    apple2_debug_write(m, addr, value);
}

static uint16_t sp_stack_pop16(apple2_t *m)
{
    CPU *cpu = &m->cpu.cpu;
    uint8_t lo;
    uint8_t hi;

    if (++cpu->sp >= 0x200u) {
        cpu->sp = 0x100u;
    }
    lo = sp_cpu_read(m, cpu->sp);
    if (++cpu->sp >= 0x200u) {
        cpu->sp = 0x100u;
    }
    hi = sp_cpu_read(m, cpu->sp);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static int sp_slot_for_trap(const apple2_t *m)
{
    int slot = m->strobed_slot;
    if (slot >= 1 && slot <= 7 && m->slot_type[slot] == SLOT_TYPE_SMARTPORT) {
        return slot;
    }
    /* Fallback: first SmartPort with mounted media (rare unlatched call). */
    for (slot = 1; slot <= 7; slot++) {
        if (m->slot_type[slot] == SLOT_TYPE_SMARTPORT &&
            sp_unit_mounted(&m->sp_device[slot], 0)) {
            return slot;
        }
    }
    return -1;
}

static int sp_unit_to_device(uint8_t unit)
{
    /* SmartPort unit 0 = controller; 1.. = devices (1-based). */
    if (unit == 0u) {
        return -1;
    }
    return (int)(unit - 1u);
}

static void sp_set_result(apple2_t *m, uint8_t err)
{
    CPU *cpu = &m->cpu.cpu;
    cpu->A = err;
    if (err == SP_SUCCESS) {
        cpu->C = 0;
        cpu->Z = 1;
        cpu->N = 0;
    } else {
        cpu->C = 1;
        cpu->Z = 0;
        cpu->N = (err & 0x80u) != 0;
    }
}

/*
 * STATUS code 0 for unit 0: device count + reserved.
 * STATUS code 0 for unit N: general status + 24-bit block count.
 */
static uint8_t sp_do_status(apple2_t *m, int slot, uint8_t unit, uint16_t list,
                            uint8_t code)
{
    SP_DEVICE *spd = &m->sp_device[slot];
    int device;

    if (code != 0u) {
        return SP_IO_ERROR; /* only device-status for now */
    }

    if (unit == 0u) {
        uint8_t n = 0;
        if (sp_unit_mounted(spd, 0)) {
            n++;
        }
        if (sp_unit_mounted(spd, 1)) {
            n++;
        }
        sp_cpu_write(m, list, n);
        sp_cpu_write(m, (uint16_t)(list + 1u), 0);
        sp_cpu_write(m, (uint16_t)(list + 2u), 0);
        sp_cpu_write(m, (uint16_t)(list + 3u), 0);
        sp_cpu_write(m, (uint16_t)(list + 4u), 0);
        sp_cpu_write(m, (uint16_t)(list + 5u), 0);
        sp_cpu_write(m, (uint16_t)(list + 6u), 0);
        sp_cpu_write(m, (uint16_t)(list + 7u), 0);
        return SP_SUCCESS;
    }

    device = sp_unit_to_device(unit);
    if (device < 0 || device > 1 || !sp_unit_mounted(spd, device)) {
        return SP_IO_ERROR;
    }

    {
        uint32_t blocks;
        uint8_t gen;

        if (spd->backend[device] == SP_BACKEND_HOSTFS) {
            hostfs_maybe_refresh(spd->hostfs[device]);
            blocks = hostfs_total_blocks(spd->hostfs[device]);
            gen = 0xF0u; /* block, write, read, online (no format) */
        } else {
            UTIL_FILE *f = &spd->sp_files[device];
            blocks = (uint32_t)(f->file_size / SP_BLOCK_SIZE);
            gen = 0xF8u; /* block, write, read, online, format */
        }

        sp_cpu_write(m, list, gen);
        sp_cpu_write(m, (uint16_t)(list + 1u), (uint8_t)(blocks & 0xFFu));
        sp_cpu_write(m, (uint16_t)(list + 2u), (uint8_t)((blocks >> 8) & 0xFFu));
        sp_cpu_write(m, (uint16_t)(list + 3u), (uint8_t)((blocks >> 16) & 0xFFu));
    }
    return SP_SUCCESS;
}

static uint8_t sp_do_read_block(apple2_t *m, int slot, uint8_t unit, uint16_t buf,
                               uint32_t block)
{
    SP_DEVICE *spd = &m->sp_device[slot];
    int device = sp_unit_to_device(unit);
    size_t i;

    if (device < 0 || device > 1) {
        return SP_IO_ERROR;
    }

    spd->sp_buffer[0] = SP_CMD_READ_BLOCK;
    spd->sp_buffer[1] = (uint8_t)(device << 7);
    spd->sp_buffer[2] = (uint8_t)(block & 0xFFu);
    spd->sp_buffer[3] = (uint8_t)((block >> 8) & 0xFFu);
    if ((block >> 16) != 0u) {
        /* Existing sp_read is 16-bit block only. */
        return SP_IO_ERROR;
    }
    sp_read(m, slot);
    if (spd->sp_buffer[0] != SP_SUCCESS) {
        return spd->sp_buffer[0];
    }
    for (i = 0; i < SP_BLOCK_SIZE; i++) {
        sp_cpu_write(m, (uint16_t)(buf + (uint16_t)i), spd->sp_buffer[1 + i]);
    }
    return SP_SUCCESS;
}

static uint8_t sp_do_write_block(apple2_t *m, int slot, uint8_t unit, uint16_t buf,
                                uint32_t block)
{
    SP_DEVICE *spd = &m->sp_device[slot];
    int device = sp_unit_to_device(unit);
    size_t i;

    if (device < 0 || device > 1) {
        return SP_IO_ERROR;
    }

    for (i = 0; i < SP_BLOCK_SIZE; i++) {
        spd->sp_buffer[4 + i] = sp_cpu_read(m, (uint16_t)(buf + (uint16_t)i));
    }
    spd->sp_buffer[0] = SP_CMD_WRITE_BLOCK;
    spd->sp_buffer[1] = (uint8_t)(device << 7);
    spd->sp_buffer[2] = (uint8_t)(block & 0xFFu);
    spd->sp_buffer[3] = (uint8_t)((block >> 8) & 0xFFu);
    if ((block >> 16) != 0u) {
        return SP_IO_ERROR;
    }
    sp_write(m, slot);
    return spd->sp_buffer[0];
}

bool sp_host_trap(apple2_t *m)
{
    uint16_t pc;
    int slot;
    uint16_t stacked;
    uint16_t cmd_addr;
    uint8_t cmd;
    uint16_t plist;
    uint8_t param_count;
    uint8_t unit;
    uint16_t ptr;
    uint8_t err = SP_IO_ERROR;

    if (m == NULL || !m->ready) {
        return false;
    }

    pc = m->cpu.cpu.pc;
    if (pc != SP_ENTRY_C800 && pc != SP_ENTRY_C89B && pc != SP_ENTRY_C9AA) {
        return false;
    }

    slot = sp_slot_for_trap(m);
    if (slot < 0) {
        return false;
    }

    /*
     * Inline SmartPort call after JSR:
     *   jsr entry
     *   .byte cmd
     *   .word param_list
     * Stack holds (addr of last byte of JSR); +1 → cmd.
     */
    stacked = sp_stack_pop16(m);
    cmd_addr = (uint16_t)(stacked + 1u);
    cmd = sp_cpu_read(m, cmd_addr);
    plist = (uint16_t)(sp_cpu_read(m, (uint16_t)(cmd_addr + 1u)) |
                       ((uint16_t)sp_cpu_read(m, (uint16_t)(cmd_addr + 2u)) << 8));
    m->cpu.cpu.pc = (uint16_t)(cmd_addr + 3u);

    param_count = sp_cpu_read(m, plist);
    unit = sp_cpu_read(m, (uint16_t)(plist + 1u));
    ptr = (uint16_t)(sp_cpu_read(m, (uint16_t)(plist + 2u)) |
                     ((uint16_t)sp_cpu_read(m, (uint16_t)(plist + 3u)) << 8));

    (void)param_count;

    switch (cmd) {
    case SP_CMD_STATUS: {
        uint8_t code = sp_cpu_read(m, (uint16_t)(plist + 4u));
        err = sp_do_status(m, slot, unit, ptr, code);
        break;
    }
    case SP_CMD_READ_BLOCK: {
        uint32_t block =
            (uint32_t)sp_cpu_read(m, (uint16_t)(plist + 4u)) |
            ((uint32_t)sp_cpu_read(m, (uint16_t)(plist + 5u)) << 8) |
            ((uint32_t)sp_cpu_read(m, (uint16_t)(plist + 6u)) << 16);
        err = sp_do_read_block(m, slot, unit, ptr, block);
        break;
    }
    case SP_CMD_WRITE_BLOCK: {
        uint32_t block =
            (uint32_t)sp_cpu_read(m, (uint16_t)(plist + 4u)) |
            ((uint32_t)sp_cpu_read(m, (uint16_t)(plist + 5u)) << 8) |
            ((uint32_t)sp_cpu_read(m, (uint16_t)(plist + 6u)) << 16);
        err = sp_do_write_block(m, slot, unit, ptr, block);
        break;
    }
    default:
        err = SP_IO_ERROR;
        break;
    }

    sp_set_result(m, err);
    /* Host trap substitutes for firmware: charge a few Φ0 for bookkeeping. */
    m->cpu.cpu.cycles += 12ull;
    m->cpu.cpu.X = 0;
    return true;
}

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"

static void apple2_cofig_from_ini(APPLE2 *m, INI_STORE *ini_store) {
    const char *val = ini_get(ini_store, "machine", "model");
    if(val && 0 == stricmp(val, "plus")) {
        m->model = MODEL_APPLE_II_PLUS;
        m->cpu.class = CPU_6502;
        m->ram_size = 64 * 1024;
    }
}

static void apple2_slot_setup(APPLE2 *m, INI_STORE *ini_store) {
    INI_SECTION *s;

    s = ini_find_section(ini_store, "diskii");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            int slot, device;

            if(sscanf(key, "%*1[Ss]%d%*1[Dd]%d", &slot, &device) == 2) {
                if(slot >= 1 && slot <= 7 && device >= 0 && device <= 1) {
                    slot_add_card(m, slot, SLOT_TYPE_DISKII, &m->diskii_controller[slot],
                                  m->roms.blocks[ROM_DISKII_16SECTOR].bytes, NULL);
                    if(val[0]) {
                        char *fn;
                        int index = 0;
                        int str_len = strlen(val);
                        while((fn = util_extract_fqn(val, str_len, &index))) {
                            diskii_mount(m, slot, device, fn);
                            free(fn);
                        }
                        diskii_mount_image(m, slot, device, 0);
                    }
                }
            }
        }
    }

    s = ini_find_section(ini_store, "smartport");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            int slot, device;

            if(sscanf(key, "%*1[Ss]%d%*1[Dd]%d", &slot, &device) == 2) {
                if(slot >= 1 && slot <= 7 && device >= 0 && device <= 1) {
                    slot_add_card(m, slot, SLOT_TYPE_SMARTPORT, &m->sp_device[slot],
                                  &m->roms.blocks[ROM_SMARTPORT].bytes[slot * 0x100], NULL);
                }
                if(val[0]) {
                    int index = 0;
                    int str_len = strlen(val);
                    char *fn = util_extract_fqn(val, str_len, &index);
                    sp_mount(m, slot, device, fn);
                    free(fn);
                }
            } else if(stricmp(key, "bs") == 0) {
                if(sscanf(val, "%d", &slot) == 1 && slot >= 1 && slot <= 7) {
                    if(m->sp_device[slot].sp_files[0].is_file_open) {
                        m->cpu.pc = 0xc000 + slot * 0x100;
                    }
                }
            }
        }
    }

    s = ini_find_section(ini_store, "video");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            int slot, n;

            if(!m->model && sscanf(key, "%*1[Ss]%d%n", &slot, &n) == 1 && n == 2 &&
                    stricmp(&key[2], "dev") == 0) {
                if(slot >= 1 && slot < 8) {
                    if(A2_OK == franklin_display_init(&m->franklin_display)) {
                        slot_add_card(m, slot, SLOT_TYPE_VIDEX_API, &m->franklin_display,
                                      &m->roms.blocks[ROM_FRANKLIN_ACE_DISPLAY].bytes[0x600], franklin_display_map_cx_rom);
                        memset(m->ram.RAM_WATCH + 0xCC00, 1, 0x200);
                        set_flags(m->state_flags, A2S_FRANKLIN80INSTALLED);
                    }
                }
            }
        }
    }
}

// Set MACHINE up as an Apple II
int apple2_init(APPLE2 *m, INI_STORE *ini_store) {

    // Clear everything
    memset(m, 0, sizeof(APPLE2));

    // Assume some basics
    m->model = MODEL_APPLE_IIEE;    //e
    m->cpu.class = CPU_65c02;       // with appropriate cpu
    m->ram_size = 128 * 1024;       // and 128k ram
    m->state_flags = A2S_TEXT;      // starting in text mode

    // Now see if the config is overridden from the cli/ini file
    apple2_cofig_from_ini(m, ini_store);

    // Allocate the RAM
    m->ram.RAM_MAIN = (uint8_t *) malloc(m->ram_size);
    m->ram.RAM_WATCH = (uint8_t *) malloc(m->ram_size);
    m->ram.RAM_LAST_WRITE = (uint64_t *) malloc(m->ram_size * sizeof(uint64_t));
    // The language_card has 16 KB.  It is set up as
    // 4K ($0000 - $0FFF) Bank 1 @ $D000 - $DFFF
    // 4K ($1000 - $1FFF) Bank 2 @ $D000 - $DFFF
    // 8K ($2000 - $3FFF)        @ $E000 - $FFFF
    // That's 16K but 2x for AUX version in IIe (allocated on ][+ as well)
    // Allocate the WATCH RAM (for breakpoints and IO callbacks)
    m->ram.RAM_LC = (uint8_t *) malloc(32 * 1024 * sizeof(uint8_t));
    m->ram.RAM_LC_WATCH = (uint8_t *) malloc(32 * 1024 * sizeof(uint8_t));
    m->ram.RAM_LC_LAST_WRITE = (uint64_t *) malloc(32 * 1024 * sizeof(uint64_t));
    if(!m->ram.RAM_MAIN || !m->ram.RAM_LC || !m->ram.RAM_WATCH || 
        !m->ram.RAM_LC_WATCH || !m->ram.RAM_LAST_WRITE || !m->ram.RAM_LC_LAST_WRITE) {
        return A2_ERR;
    }
    // Init RAM to a fixed pattern
    util_memset32(m->ram.RAM_MAIN, 0x0000ffff, m->ram_size / 4);
    util_memset32(m->ram.RAM_LC, 0x0000ffff, 32 * 1024 / 4);
    // Set last write to all zero's
    memset(m->ram.RAM_LAST_WRITE, 0, m->ram_size * sizeof(uint64_t));
    memset(m->ram.RAM_LC_LAST_WRITE, 0, 32 * 1024 * sizeof(uint64_t));
    // Set the watch to not watching anything
    memset(m->ram.RAM_WATCH, WATCH_NONE, m->ram_size);
    memset(m->ram.RAM_LC_WATCH, WATCH_NONE, 32 * 1024 * sizeof(uint8_t));

    // And IO area floating bus is a lot of 160's
    memset(&m->ram.RAM_MAIN[0xC001], 0xA0, 0x0FFE);
    if(m->ram_size >= 0x1D000) {
        memset(&m->ram.RAM_MAIN[0x10000 + 0xC001], 0xA0, 0x0FFE);
    }

    // Populate the ROMs
    if(A2_OK != rom_init(&m->roms, ROM_NUM_ROMS)) {
        return A2_ERR;
    }
    if(m->model == MODEL_APPLE_II_PLUS) {
        rom_add(&m->roms, ROM_APPLE2, 0xD000, a2p_rom_size, a2p_rom);
        rom_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2p_character_rom_size, a2p_character_rom);
    } else {
        rom_add(&m->roms, ROM_APPLE2, 0xD000, 0x3000, &a2ee_rom[0X1000]);
        rom_add(&m->roms, ROM_APPLE2_SLOTS, 0xC000, 0x1000, a2ee_rom);
        rom_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2ee_character_rom_size, a2ee_character_rom);
    }
    rom_add(&m->roms, ROM_DISKII_13SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_13SECTOR]);
    rom_add(&m->roms, ROM_DISKII_16SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_16SECTOR]);
    rom_add(&m->roms, ROM_SMARTPORT, 0xC000, smartport_rom_size, smartport_rom);
    rom_add(&m->roms, ROM_FRANKLIN_ACE_DISPLAY, 0x0000, franklin_ace_display_rom_size, franklin_ace_display_rom);
    rom_add(&m->roms, ROM_FRANKLIN_ACE_CHARACTER, 0x0000, franklin_ace_character_rom_size, franklin_ace_character_rom);

    // Create all the pages
    if(A2_OK != pages_init(&m->pages, BANK_SIZE)) {
        return A2_ERR;
    }

    // Map main write ram
    pages_map(&m->pages, PAGE_MAP_READ, 0, BANK_SIZE, &m->ram);
    pages_map(&m->pages, PAGE_MAP_WRITE, 0, BANK_SIZE, &m->ram);

    // Map the rom ($D000-$FFFF) as read pages
    pages_map_rom_block(&m->pages, &m->roms.blocks[ROM_APPLE2], &m->ram);

    // Map watch area checks - start with no watch
    // Add the IO ports by flagging them as 1 in the RAM watch (incl LC 0xc08x area)
    memset(&m->ram.RAM_WATCH[0xC000], WATCH_IO_PORT, 0x90);
    if(m->model == MODEL_APPLE_IIEE) {
        // On a //e, also watch Slot 3
        memset(&m->ram.RAM_WATCH[0xC0B0], WATCH_IO_PORT, 0x0F);
        memset(&m->ram.RAM_WATCH[0xC300], WATCH_IO_PORT, 0xFF);
        // The other slot areas are added in slot_add_card as-needed
    }
    // Watch the location to clear slot rom
    m->ram.RAM_WATCH[CLRROM] = WATCH_IO_PORT;

    // Select the io handlers for this model
    io_setup(m);

    // Init the CPU to cold-start by jumping to ROM address at 0xfffc
    cpu_init(m);

    for(int slot = 1; slot < 8; slot++) {
        diskii_controller_init(&m->diskii_controller[slot]);
    }

    // Configure the slots (based on ini_store)
    apple2_slot_setup(m, ini_store);

    // Shadow the slot read area for easy restore - this captures maps set by slot_add_card, and outside of slot_add_card
    memcpy(m->rom_shadow_pages, &m->pages.read_pages[(0xC000 / PAGE_SIZE)], sizeof(uint8_t *) * (0xC800 - 0xC000) / PAGE_SIZE);

    return A2_OK;
}

void apple2_machine_reset(APPLE2 *m) {
    // Clear the screen
    memset(&m->ram.RAM_MAIN[0x0400], 0xA0, 0x400);

    // Select the io handlers for this model
    io_setup(m);

    // Set up CPU
    cpu_init(m);
}

void apple2_set_callbacks(APPLE2 *m, A2OUT_CB *a2rt_cb) {
    m->a2out_cb = *a2rt_cb;
}

// Clean up the Apple II
void apple2_shutdown(APPLE2 *m) {
    // speaker_shutdown(&m->speaker);
    diskii_shutdown(m);
    sp_shutdown(m);
    if(tst_flags(m->state_flags, A2S_FRANKLIN80INSTALLED)) {
        franklin_display_shutdown(&m->franklin_display);
    }
    // pages
    free(m->pages.read_pages);
    m->pages.read_pages = NULL;
    free(m->pages.write_pages);
    m->pages.write_pages = NULL;
    free(m->pages.watch_read_pages);
    m->pages.watch_read_pages = NULL;
    free(m->pages.watch_write_pages);
    m->pages.watch_write_pages = NULL;
    m->pages.num_pages = 0;
    // ROMS
    free(m->roms.blocks);
    m->roms.blocks = 0;
    m->roms.num_blocks = 0;
    // RAM
    free(m->ram.RAM_MAIN);
    m->ram.RAM_MAIN = NULL;
    free(m->ram.RAM_WATCH);
    m->ram.RAM_WATCH = NULL;
    free(m->ram.RAM_LC);
    m->ram.RAM_LC = NULL;
    free(m->ram.RAM_LAST_WRITE);
    m->ram.RAM_LAST_WRITE = NULL;
}

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"
#include "a2sft_p.h"
#include "a2sft_ee.h"

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
                        diskii_mount(m, slot, device, val);
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
                    sp_mount(m, slot, device, val);
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
                        memset(m->RAM_WATCH + 0xCC00, 1, 0x200);
                        m->franklin80installed = 1;
                    }
                }
            }
        }
    }
}

// Handle the Apple II sofswitches when read
uint8_t apple2_softswitch_read_callback(APPLE2 *m, uint16_t address) {
    if(m->model) {
        return apple2_softswitch_read_callback_IIe(m, address);
    }
    return apple2_softswitch_read_callback_IIplus(m, address);
}

// Handle the Apple ][ softswitches when written to
void apple2_softswitch_write_callback(APPLE2 *m, uint16_t address, uint8_t value) {
    if(m->model) {
        apple2_softswitch_write_callback_IIe(m, address, value);
    } else {
        apple2_softswitch_write_callback_IIplus(m, address, value);
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
    m->text = 1;                    // staring in text mode

    // Now see if the config is overridden from the cli/ini file
    apple2_cofig_from_ini(m, ini_store);

    // Allocate the RAM
    m->RAM_MAIN = (uint8_t *) malloc(m->ram_size);
    // Allocate the WATCH RAM (for breakpoints and IO callbacks)
    m->RAM_WATCH = (uint8_t *) malloc(m->ram_size);
    if(!m->RAM_MAIN || !m->RAM_WATCH) {
        return A2_ERR;
    }

    // Init RAM to a fixed pattern
    util_memset32(m->RAM_MAIN, 0x0000ffff, m->ram_size / 4);
    // And IO area floating bus is a lot of 160's
    memset(&m->RAM_MAIN[0xC001], 0xA0, 0x0FFE);
    if(m->ram_size >= 0x1D000) {
        memset(&m->RAM_MAIN[0x10000 + 0xC001], 0xA0, 0x0FFE);
    }

    // The language_card has 16 KB.  It is set up as
    // 4K ($0000 - $0FFF) Bank 1 @ $D000 - $DFFF
    // 4K ($1000 - $1FFF) Bank 2 @ $D000 - $DFFF
    // 8K ($2000 - $3FFF)        @ $E000 - $FFFF
    // That's 16K but 2x for AUX version in IIe (allocated on ][+ as well) // SQW
    m->RAM_LC = (uint8_t *) malloc(32 * 1024);
    if(!m->RAM_LC) {
        return A2_ERR;
    }
    util_memset32(m->RAM_LC, 0x0000ffff, 32 * 1024 / 4);

    // RAM
    if(!memory_init(&m->ram, m->model ? 2 : 1)) {
        return A2_ERR;
    }
    memory_add(&m->ram, 0, 0x0000, BANK_SIZE, &m->RAM_MAIN[0]);
    if(m->model) {
        memory_add(&m->ram, 1, 0x0000, BANK_SIZE, &m->RAM_MAIN[BANK_SIZE]);
    }
    // ROMS
    if(!memory_init(&m->roms, ROM_NUM_ROMS)) {
        return A2_ERR;
    }
    if(!m->model) {
        memory_add(&m->roms, ROM_APPLE2, 0xD000, a2p_rom_size, a2p_rom);
        memory_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2p_character_rom_size, a2p_character_rom);
    } else {
        memory_add(&m->roms, ROM_APPLE2, 0xD000, 0x3000, &a2ee_rom[0X1000]);
        memory_add(&m->roms, ROM_APPLE2_SLOTS, 0xC000, 0x1000, a2ee_rom);
        memory_add(&m->roms, ROM_APPLE2_CHARACTER, 0x0000, a2ee_character_rom_size, a2ee_character_rom);
    }
    memory_add(&m->roms, ROM_DISKII_13SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_13SECTOR]);
    memory_add(&m->roms, ROM_DISKII_16SECTOR, 0xC000, 256, disk2_rom[DSK_ENCODING_16SECTOR]);
    memory_add(&m->roms, ROM_SMARTPORT, 0xC000, smartport_rom_size, smartport_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_DISPLAY, 0x0000, franklin_ace_display_rom_size, franklin_ace_display_rom);
    memory_add(&m->roms, ROM_FRANKLIN_ACE_CHARACTER, 0x0000, franklin_ace_character_rom_size, franklin_ace_character_rom);

    // PAGES
    if(!pages_init(&m->read_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->write_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }
    if(!pages_init(&m->watch_pages, BANK_SIZE / PAGE_SIZE)) {
        return A2_ERR;
    }

    // Map main write ram
    pages_map(&m->write_pages, 0, BANK_SIZE / PAGE_SIZE, m->ram.blocks[MAIN_RAM].bytes);
    // Map read ram (same as write ram in this case)
    pages_map(&m->read_pages, 0, BANK_SIZE / PAGE_SIZE, m->ram.blocks[MAIN_RAM].bytes);
    // Map the rom ($D000-$FFFF) as read pages
    pages_map_memory_block(&m->read_pages, &m->roms.blocks[ROM_APPLE2]);

    // Map watch area checks - start with no watch
    memset(m->RAM_WATCH, WATCH_NONE, BANK_SIZE);
    // Add the IO ports by flagging them as 1 in the RAM watch
    memset(&m->RAM_WATCH[0xC000], WATCH_IO_PORT, 0xFF);
    if(m->model) {
        // On a //e, also watch Slot 3
        memset(&m->RAM_WATCH[0xC300], WATCH_IO_PORT, 0xFF);
    }
    // Watch the location to clear slot rom
    m->RAM_WATCH[CLRROM] = WATCH_IO_PORT;
    // Set up the Watch Pages (RAM_WATCH can be changed without re-doing the map)
    pages_map(&m->watch_pages, 0, BANK_SIZE / PAGE_SIZE, m->RAM_WATCH);

    // Set up the Language Card
    language_card_init(m);

    // Init the CPU to cold-start by jumping to ROM address at 0xfffc
    cpu_init(m);

    // Configure the slots (based on ini_store)
    apple2_slot_setup(m, ini_store);

    // Shadow the slot read area for easy restore - this captures maps set by slot_add_card, and outside of slot_add_card
    memcpy(m->rom_shadow_pages, &m->read_pages.pages[(0xC000 / PAGE_SIZE)], sizeof(uint8_t *) * (0xC800 - 0xC000) / PAGE_SIZE);

    return A2_OK;
}

void apple2_machine_reset(APPLE2 *m) {
    // A2 state_flags reset (keep model), setting text mode
    int model = m->model;
    int f80 = m->franklin80installed;
    m->state_flags = 0;
    m->model = model;
    m->franklin80installed = f80;
    m->text = 1;

    // Reset LC
    language_card_init(m);

    // Clear the screen
    memset(&m->RAM_MAIN[0x0400], 0xA0, 0x400);

    //e - Set up soft-switches
    if(m->model) {
        set_memory_map(m);
        apple2_softswitch_write_callback_IIe(m, CLRCXROM, 0);
    }

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
    free(m->RAM_MAIN);
    m->RAM_MAIN = NULL;
    free(m->RAM_WATCH);
    m->RAM_WATCH = NULL;
    free(m->RAM_LC);
    m->RAM_LC = NULL;
}

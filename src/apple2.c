// Apple ][+ and //e Emhanced emulator
// Stefan Wessels, 2024
// This is free and unencumbered software released into the public domain.

#include "header.h"
#include "a2sft_p.h"
#include "a2sft_ee.h"

// Set MACHINE up as an Apple II
int apple2_configure(APPLE2 *m) {

    // Clear the whole emulated machine
    memset(m, 0, sizeof(APPLE2));
    // Set text mode on
    m->text = 1;
    // Init the ini config storage
    ini_init(&m->ini_store);
    // Load config from ini file
    if(A2_OK != util_ini_load_file("./apple2.ini", ini_add, (void *)&m->ini_store)) {
        ini_add(&m->ini_store, "Machine", "Model", "enhanced ; plus | enh (//e Enhanced)");
        ini_add(&m->ini_store, "Machine", "Turbo", "1, 8, 16, max ; Float.  F3 cycles - max is flat out");
        ini_add(&m->ini_store, "Display", "scale", "1.0 ; Uniformly scale Application Window");
        ini_add(&m->ini_store, "Display", "disk_leds", "1 ; Show disk activity LEDs");
        ini_add(&m->ini_store, "Video", ";slot", "3 ; Slot where an ][+ 80 col card is inserted ");
        ini_add(&m->ini_store, "Video", ";device", "Franklin Ace Display ; 80 Column Videx like card");
        ini_add(&m->ini_store, "DiskII", "slot", "6 ; This says a slot contains a disk II controller");
        ini_add(&m->ini_store, "DiskII", "disk0", "; file name of a NIB floppy image, NOT in quotes");
        ini_add(&m->ini_store, "DiskII", "disk1", "; ./disks/Apple DOS 3.3 January 1983.nib ; example usage");
        ini_add(&m->ini_store, "SmartPort", "slot", "5 ; This says a slot contains a smartport");
        ini_add(&m->ini_store, "SmartPort", "disk0", " ; file name of an image, NOT in quotes");
        ini_add(&m->ini_store, "SmartPort", "disk1", "");
        ini_add(&m->ini_store, "SmartPort", "boot", "0 ; any value other than 0 will cause a boot of disk0");
        ini_add(&m->ini_store, "SmartPort", "slot", "7 ; There can be multiple slots");
        ini_add(&m->ini_store, "SmartPort", "disk0", "");
        ini_add(&m->ini_store, "SmartPort", "disk1", "");
        ini_add(&m->ini_store, "SmartPort", "boot", "0 ; last listed non-zero boot devices' disk0 will boot");
        ini_add(&m->ini_store, "Debug", ";break", "pc, restore, 0, 0 are the defaults for break =");
        ini_add(&m->ini_store, "Debug", ";break", "<address[-address]>[,pc|read|write|access][,restore | fast | slow | tron | trona | troff][, count[, reset]]]");
        util_ini_save_file("apple2.ini", &m->ini_store);
    }
    // Configure the type of machine (II+ or //e Enhanced, based on ini_store)
    apple2_machine_setup(m);
    // Set the Turbo state
    if(!m->turbo_count) {
        m->turbo_count = 2;
        m->turbo = (double*)malloc(m->turbo_count * sizeof(double));
        if(m->turbo) {
            m->turbo[0] = 1.0;
			m->turbo[1] = -1.0;
        } else {
            return A2_ERR;
        }
    }
    m->turbo_active = m->turbo[m->turbo_index];

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

    // Install the watch callbacks
    m->callback_write = apple2_softswitch_write_callback;
    m->callback_read = apple2_softswitch_read_callback;
    m->callback_breakpoint = breakpoint_callback;
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
    apple2_slot_setup(m);

    // Shadow the slot read area for easy restore - this captures maps set by slot_add_card, and outside of slot_add_card
    memcpy(m->rom_shadow_pages, &m->read_pages.pages[(0xC000 / PAGE_SIZE)], sizeof(uint8_t *) * (0xC800 - 0xC000) / PAGE_SIZE);

    return A2_OK;
}

void apple2_machine_reset(APPLE2 *m) {

    // emu flags reset only debugger state
    m->stopped = 0;
    m->step = 0;

    // A2 state_flags reset, setting text mode
    m->state_flags = 0;
    m->text = 1;
    
    cpu_init(m);

    // Clear the screen
    memset(&m->RAM_MAIN[0x0400], 0xA0, 0x400);

    language_card_init(m);

    //e - Set up soft-switches
    if(m->model) {
        set_memory_map(m);
        apple2_softswitch_write_callback_IIe(m, CLRCXROM, 0);
    }
}

void apple2_machine_setup(APPLE2 *m) {
    INI_SECTION *s;
    int slot_number = -1;

    m->model = MODEL_APPLE_IIEE;
    m->cpu.class = CPU_65c02;
    m->ram_size = 128 * 1024;

    s = ini_find_section(&m->ini_store, "machine");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;

            if(0 == stricmp(key, "model")) {
                if(0 == stricmp(val, "plus")) {
                    m->model = MODEL_APPLE_II_PLUS;
                    m->cpu.class = CPU_6502;
                    m->ram_size = 64 * 1024;
                }
            }

            if(0 == stricmp(key, "turbo")) {
                // Count the commas to know how many turbo states there are
                const char *s = val;
                m->turbo_count = 1;
                while(*s) {
                    if(*s++ == ',') {
                        m->turbo_count++;
                    }
                }
                // Allocare the turbo's array
                m->turbo = (double*)malloc(m->turbo_count * sizeof(double));
                if(m->turbo) {
                    // Convert the nubers to +float and any unknowns (non-float)
                    // to -1 (max speed)
                    s = val;
                    for(int i = 0; i < m->turbo_count; i++) {
                        int l = sscanf(s, "%lf", &m->turbo[i]);
                        if(l == -1) {
                            // no value - make it a turbo of 1.0
                            m->turbo[i] = 1.0;
                        } else if(l == 0) {
                            // bad conversion - max
                            m->turbo[i] = -1.0;
                        } else {
                            // normal conversion - make it the positive to be sure
                            m->turbo[i] = fabs(m->turbo[i]);
                            // treat overflow as "max speed"
                            if(isinf(m->turbo[i])) {
                                m->turbo[i] = -1.0;
                            }
                        }
                        // scan to the comma
                        while(*s && *s != ',') {
                            s++;
                        }
                        // Skip the comma
                        if(*s) {
                            s++;
                        }
                    }
                } else {
                    m->turbo_count = 0;
                }
            }
        }
    }
}

void apple2_slot_setup(APPLE2 *m) {
    INI_SECTION *s;
    int slot_number = 0;
    
    // No slot mapped in C800 range to start
    m->mapped_slot = 0;

    s = ini_find_section(&m->ini_store, "diskii");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
                if(slot_number >= 1 && slot_number < 8) {
                    slot_add_card(m, slot_number, SLOT_TYPE_DISKII, &m->diskii_controller[slot_number],
                                  m->roms.blocks[ROM_DISKII_16SECTOR].bytes, NULL);
                    m->diskii_controller[slot_number].diskii_drive[0].quarter_track_pos = rand() % DISKII_QUATERTRACKS;
                    m->diskii_controller[slot_number].diskii_drive[1].quarter_track_pos = rand() % DISKII_QUATERTRACKS;
                }
            } else if(slot_number >= 1 && slot_number < 8) {
                if(0 == stricmp(key, "disk0")) {
                    diskii_mount(m, slot_number, 0, val);
                } else if(0 == stricmp(key, "disk1")) {
                    diskii_mount(m, slot_number, 1, val);
                }
            }
        }
    }

    s = ini_find_section(&m->ini_store, "smartport");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
                if(slot_number >= 1 && slot_number < 8) {
                    slot_add_card(m, slot_number, SLOT_TYPE_SMARTPORT, &m->sp_device[slot_number],
                                  &m->roms.blocks[ROM_SMARTPORT].bytes[slot_number * 0x100], NULL);
                }
            } else if(slot_number >= 1 && slot_number < 8) {
                if(0 == stricmp(key, "disk0")) {
                    sp_mount(m, slot_number, 0, val);
                } else if(0 == stricmp(key, "disk1")) {
                    sp_mount(m, slot_number, 1, val);
                } else if(0 == stricmp(key, "boot")) {
                    if(0 != strcmp(val, "0") && m->sp_device[slot_number].sp_files[0].is_file_open) {
                        // The rom doesn't get a chance to run but fortunately ProDOS does just fine
                        m->cpu.pc = 0xc000 + slot_number * 0x100;
                    }
                }
            }
        }
    }

    s = ini_find_section(&m->ini_store, "video");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;
            if(0 == stricmp(key, "slot")) {
                sscanf(val, "%d", &slot_number);
            }
            if(0 == stricmp(key, "device") && !m->model) { // Prevent the enhanced from adding this card
                if(slot_number >= 1 && slot_number < 8) {
                    if(A2_OK == franklin_display_init(&m->franklin_display)) {
                        slot_add_card(m, slot_number, SLOT_TYPE_VIDEX_API, &m->franklin_display,
                                      &m->roms.blocks[ROM_FRANKLIN_ACE_DISPLAY].bytes[0x600], franklin_display_map_cx_rom);
                        memset(m->RAM_WATCH + 0xCC00, 1, 0x200);
                        m->franklin80installed = 1;
                    }
                }
            }
        }
    }
}

// Clean up the Apple II
void apple2_shutdown(APPLE2 *m) {
    speaker_shutdown(&m->speaker);
    diskii_shutdown(m);
    free(m->RAM_MAIN);
    m->RAM_MAIN = NULL;
    free(m->RAM_WATCH);
    m->RAM_WATCH = NULL;
    free(m->RAM_LC);
    m->RAM_LC = NULL;
    m->turbo_count = 0;
    free(m->turbo);
    m->turbo = NULL;
    m->viewport = NULL;
    SDL_free(m->clipboard_text);
    m->clipboard_text = 0;
    m->clipboard_index = 0;
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

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common_lib.h"

static int config_ini_section_has_slot(INI_STORE *st, const char *section, int slot) {
    INI_SECTION *s = ini_find_section(st, section);
    if(!s) {
        return 0;
    }
    for(int i = 0; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        int key_slot, device;
        if(sscanf(kv->key, "%*1[Ss]%d%*1[Dd]%d", &key_slot, &device) == 2) {
            if(key_slot == slot && device >= 0 && device <= 1) {
                return 1;
            }
        }
    }
    return 0;
}

static int config_ini_video_has_slot(INI_STORE *st, int slot) {
    INI_SECTION *s = ini_find_section(st, "Video");
    if(!s) {
        return 0;
    }
    for(int i = 0; i < s->kv.items; i++) {
        INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
        int key_slot, n;
        if(sscanf(kv->key, "%*1[Ss]%d%n", &key_slot, &n) == 1 && n == 2 && 0 == stricmp(&kv->key[2], "dev")) {
            if(key_slot == slot) {
                return 1;
            }
        }
    }
    return 0;
}


void cmn_config_from_ini(MACHINE_CONFIG *mc, INI_STORE *ini_store) {
    memset(mc, 0, sizeof(*mc));
    mc->active_tab = MACHINE_CONFIG_TAB_MACHINE;
    mc->model = MODEL_APPLE_IIEE;
    mc->ui_sel = 0;
    mc->disk_leds = 0;
    mc->remember_ini = 0;
    mc->asm_dest_flags = 0;

    const char *val = ini_get(ini_store, "Machine", "Model");
    if(val && 0 == stricmp(val, "plus")) {
        mc->model = MODEL_APPLE_II_PLUS;
    }

    for(int i = 0; i < 7; i++) {
        int slot = i + 1;
        int sel = MACHINE_SLOT_EMPTY;
        if(slot == 3 && mc->model == MODEL_APPLE_II_PLUS && config_ini_video_has_slot(ini_store, slot)) {
            sel = MACHINE_SLOT_FRANKLIN;
        } else if(config_ini_section_has_slot(ini_store, "SmartPort", slot)) {
            sel = MACHINE_SLOT_SMARTPORT;
        } else if(config_ini_section_has_slot(ini_store, "DiskII", slot)) {
            sel = MACHINE_SLOT_DISKII;
        }
        mc->slot_sel[i] = sel;
    }

    val = ini_get(ini_store, "Config", "ui");
    if(val && 0 == stricmp(val, "text")) {
        mc->ui_sel = 1;
    }

    val = ini_get(ini_store, "Config", "wheel_speed");
    if(val) {
        mc->wheel_speed_text_len = (int)strnlen(val, sizeof(mc->wheel_speed_text) - 1);
        strncpy(mc->wheel_speed_text, val, sizeof(mc->wheel_speed_text) - 1);
        mc->wheel_speed_text[sizeof(mc->wheel_speed_text) - 1] = '\0';
    } else {
        mc->wheel_speed_text_len = snprintf(mc->wheel_speed_text, sizeof(mc->wheel_speed_text), "%d", 4);
    }

    val = ini_get(ini_store, "Config", "disk_leds");
    if(val) {
        int state = 0;
        sscanf(val, "%d", &state);
        if(0 == stricmp(val, "on") || state == 1) {
            mc->disk_leds = 1;
        }
    }

    if(ini_get(ini_store, "Config", "Save")) {
        mc->remember_ini = 1;
    }

    val = ini_get(ini_store, "Machine", "Turbo");
    if(val) {
        mc->turbo_text_len = (int)strnlen(val, sizeof(mc->turbo_text) - 1);
        strncpy(mc->turbo_text, val, sizeof(mc->turbo_text) - 1);
        mc->turbo_text[sizeof(mc->turbo_text) - 1] = '\0';
    }

    val = ini_get(ini_store, "Config", "symbols");
    if(val) {
        mc->symbols_text_len = (int)strnlen(val, sizeof(mc->symbols_text) - 1);
        strncpy(mc->symbols_text, val, sizeof(mc->symbols_text) - 1);
        mc->symbols_text[sizeof(mc->symbols_text) - 1] = '\0';
    }

    val = ini_get(ini_store, "Config", "ini_file");
    if(val) {
        mc->ini_file_text_len = (int)strnlen(val, sizeof(mc->ini_file_text) - 1);
        strncpy(mc->ini_file_text, val, sizeof(mc->ini_file_text) - 1);
        mc->ini_file_text[sizeof(mc->ini_file_text) - 1] = '\0';
    }

    val = ini_get(ini_store, "Assembler", "source");
    if(val) {
        mc->asm_source_text_len = (int)strnlen(val, sizeof(mc->asm_source_text) - 1);
        strncpy(mc->asm_source_text, val, sizeof(mc->asm_source_text) - 1);
        mc->asm_source_text[sizeof(mc->asm_source_text) - 1] = '\0';
    }

    val = ini_get(ini_store, "Assembler", "dest");
    mc->asm_dest_flags = cmn_parse_mem_dest_string(val);

    val = ini_get(ini_store, "Assembler", "reset_stack");
    if(val && (0 == stricmp(val, "yes") || 0 == stricmp(val, "1"))) {
        mc->asm_reset_stack = 1;
    }

    val = ini_get(ini_store, "Assembler", "auto_run");
    if(val && (0 == stricmp(val, "yes") || 0 == stricmp(val, "1"))) {
        mc->asm_auto_run = 1;
    }

    val = ini_get(ini_store, "Assembler", "address");
    if(val) {
        uint16_t addr = (uint16_t)strtoul(val, NULL, 0);
        mc->asm_address_text_len = snprintf(mc->asm_address_text, sizeof(mc->asm_address_text), "%04X", addr);
    }
}

static void config_ini_slot_key(char *out, size_t out_len, int slot, int device) {
    snprintf(out, out_len, "s%dd%d", slot, device);
}

static void config_ini_ensure_slot(INI_STORE *ini_store, const char *section, int slot) {
    char key[8];
    for(int device = 0; device < 2; device++) {
        config_ini_slot_key(key, sizeof(key), slot, device);
        if(!ini_get(ini_store, section, key)) {
            ini_set(ini_store, section, key, "");
        }
    }
}

static void config_ini_remove_slot(INI_STORE *ini_store, const char *section, int slot) {
    char key[8];
    for(int device = 0; device < 2; device++) {
        config_ini_slot_key(key, sizeof(key), slot, device);
        ini_remove_key(ini_store, section, key);
    }
}

void cmn_config_apply(MACHINE_CONFIG *mc, INI_STORE *ini_store) {
    const char *model_val = mc->model == MODEL_APPLE_II_PLUS ? "plus" : "enh";
    ini_set(ini_store, "Machine", "Model", model_val);

    if(mc->turbo_text_len > 0) {
        ini_set(ini_store, "Machine", "Turbo", mc->turbo_text);
    } else {
        ini_remove_key(ini_store, "Machine", "Turbo");
    }

    ini_set(ini_store, "Config", "ui", mc->ui_sel ? "text" : "gui");

    if(mc->wheel_speed_text_len > 0) {
        int speed = 0;
        if(1 == sscanf(mc->wheel_speed_text, "%d", &speed)) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", speed);
            ini_set(ini_store, "Config", "wheel_speed", buf);
        } else {
            ini_remove_key(ini_store, "Config", "wheel_speed");
        }
    } else {
        ini_remove_key(ini_store, "Config", "wheel_speed");
    }

    ini_set(ini_store, "Config", "disk_leds", mc->disk_leds ? "on" : "off");

    if(mc->remember_ini) {
        ini_set(ini_store, "Config", "Save", "yes");
    } else {
        ini_remove_key(ini_store, "Config", "Save");
    }

    if(mc->symbols_text_len > 0) {
        ini_set(ini_store, "Config", "symbols", mc->symbols_text);
    } else {
        ini_remove_key(ini_store, "Config", "symbols");
    }

    if(mc->ini_file_text_len > 0) {
        ini_set(ini_store, "Config", "ini_file", mc->ini_file_text);
    } else {
        ini_remove_key(ini_store, "Config", "ini_file");
    }

    if(mc->asm_source_text_len > 0) {
        ini_set(ini_store, "Assembler", "source", mc->asm_source_text);
    } else {
        ini_remove_key(ini_store, "Assembler", "source");
    }

    char dest_text[64];
    cmn_mem_dest_to_string(mc->asm_dest_flags, dest_text, sizeof(dest_text));
    ini_set(ini_store, "Assembler", "dest", dest_text);

    if(mc->asm_reset_stack) {
        ini_set(ini_store, "Assembler", "reset_stack", "yes");
    } else {
        ini_remove_key(ini_store, "Assembler", "reset_stack");
    }

    if(mc->asm_auto_run) {
        ini_set(ini_store, "Assembler", "auto_run", "yes");
    } else {
        ini_remove_key(ini_store, "Assembler", "auto_run");
    }

    if(mc->asm_address_text_len > 0) {
        uint16_t addr = (uint16_t)strtoul(mc->asm_address_text, NULL, 16);
        char buf[10];
        snprintf(buf, sizeof(buf), "0x%04X", addr);
        ini_set(ini_store, "Assembler", "address", buf);
    } else {
        ini_remove_key(ini_store, "Assembler", "address");
    }

    for(int i = 0; i < 7; i++) {
        int slot = i + 1;
        int sel = mc->slot_sel[i];

        if(sel == MACHINE_SLOT_DISKII) {
            config_ini_remove_slot(ini_store, "SmartPort", slot);
            if(slot == 3) {
                ini_remove_key(ini_store, "Video", "s3dev");
            }
            config_ini_ensure_slot(ini_store, "DiskII", slot);
        } else if(sel == MACHINE_SLOT_SMARTPORT) {
            config_ini_remove_slot(ini_store, "DiskII", slot);
            if(slot == 3) {
                ini_remove_key(ini_store, "Video", "s3dev");
            }
            config_ini_ensure_slot(ini_store, "SmartPort", slot);
        } else if(sel == MACHINE_SLOT_FRANKLIN && slot == 3 && mc->model == MODEL_APPLE_II_PLUS) {
            config_ini_remove_slot(ini_store, "DiskII", slot);
            config_ini_remove_slot(ini_store, "SmartPort", slot);
            ini_set(ini_store, "Video", "s3dev", "Franklin Ace Display");
        } else {
            config_ini_remove_slot(ini_store, "DiskII", slot);
            config_ini_remove_slot(ini_store, "SmartPort", slot);
            if(slot == 3) {
                ini_remove_key(ini_store, "Video", "s3dev");
            }
        }
    }
}

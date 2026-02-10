// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

// Supported Apple II Models
typedef enum {
    MODEL_APPLE_II_PLUS,
    MODEL_APPLE_IIEE,
} A2_MODEL;

typedef enum {
    MACHINE_CONFIG_TAB_MACHINE,
    MACHINE_CONFIG_TAB_EMULATOR,
    MACHINE_CONFIG_TAB_ASSEMBLER,
} MACHINE_CONFIG_TAB;

typedef enum {
    MACHINE_SLOT_EMPTY,
    MACHINE_SLOT_DISKII,
    MACHINE_SLOT_SMARTPORT,
    MACHINE_SLOT_FRANKLIN,
} MACHINE_SLOT_TYPE;

typedef enum {
    MACHINE_BROWSE_NONE,
    MACHINE_BROWSE_SYMBOLS,
    MACHINE_BROWSE_ASM_SOURCE,
    MACHINE_BROWSE_INI_FILE,
} MACHINE_BROWSE_TARGET;

typedef struct {
    int active_tab;
    int model;
    int slot_sel[7];
    int ui_sel;
    int disk_leds;
    int remember_ini;
    int save_ini;
    char wheel_speed_text[3];
    int wheel_speed_text_len;
    char turbo_text[96];
    int turbo_text_len;
    char symbols_text[PATH_MAX];
    int symbols_text_len;
    char ini_file_text[PATH_MAX];
    int ini_file_text_len;

    char asm_source_text[PATH_MAX];
    int asm_source_text_len;
    VIEW_FLAGS asm_dest_flags;
    int asm_reset_stack;
    int asm_auto_run;
    char asm_address_text[5];
    int asm_address_text_len;

    int dlg_filebrowser;
    int browse_target;
} MACHINE_CONFIG;

enum {
    CNF_CNG_NONE            = 1 << 0,
    CNF_CNG_INI_FILE_NAME   = 1 << 1,
    CNF_CNG_SAVE_ON_EXIT    = 1 << 2,
    CNF_CNG_RESTART         = 1 << 3,
};

void cmn_config_apply(MACHINE_CONFIG *mc, INI_STORE *ini_store);
void cmn_config_from_ini(MACHINE_CONFIG *mc, INI_STORE *ini_store);
int cmn_config_changed(const MACHINE_CONFIG *a, const MACHINE_CONFIG *b);

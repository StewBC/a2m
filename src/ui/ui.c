// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "ui_lib.h"

const UI_OPS null_ops = {
    .disk_read_led     = NULL,
    .disk_write_led    = NULL,
    .process_events    = NULL,
    .speaker_toggle    = NULL,
    .speaker_on_cycles = NULL,
    .render            = NULL,
    .set_runtime       = NULL,
    .set_shadow_flags = NULL,
    .shutdown          = NULL,
};

void ui_apply_ini(UI *ui, INI_STORE *ini_store) {
    int slot_number = -1;

    const char *val = ini_get(ini_store, "ui", "instance");
    if(val && stricmp(val, "text") == 0) {
        ui->class = UI_CLASS_TEXT;
    }
}

int ui_init(UI *ui, int model, INI_STORE *ini_store) {
    // Clear the ops and ptr to the actual UI
    memset(ui, 0, sizeof(UI));
    // decide which UI needs to be instanced
    ui_apply_ini(ui, ini_store);
    // Any user driven UI reconfiguration will have happened
    ui->reconfig = 0;
    // Instance the UI and have it configure itself
    if(ui->class == UI_CLASS_GUI) {
        // Instance UNK
        UNK *v = (UNK *)malloc(sizeof(UNK));
        if(!v) {
            return A2_ERR;
        }
        // Clear all
        memset(v, 0, sizeof(UNK));

        // Link through UI
        ui->user = v;
        ui->ops = &unk_ops;

        // Set a target windows default size
        v->target_rect.w = 1120;
        v->target_rect.h = 840;

        // UNK gets to init
        return unk_init(v, model, ini_store);
    } else {
        // Instance UTXT
        UTXT *v = (UTXT *)malloc(sizeof(UTXT));
        if(!v) {
            return A2_ERR;
        }
        // Clear all
        memset(v, 0, sizeof(UTXT));
        // Link through UI
        ui->user = v;
        ui->ops = &utxt_ops;

        // TXT gets to init
        return utxt_init(v, ini_store);
    }
}

void ui_shutdown(UI *ui) {
    if(ui->class == UI_CLASS_TEXT) {
        utxt_shutdown((UTXT *)ui->user);
    } else {
        unk_shutdown((UNK *)ui->user);
    }
    free(ui->user);
    ui->ops = &null_ops;
}

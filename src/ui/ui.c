// Apple ][+ and //e Emhanced emulator with assembler
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
    .set_shaddow_flags = NULL,
    .shutdown          = NULL,
};

void ui_apply_ini(UI *ui, INI_STORE *ini_store) {
    int slot_number = -1;

    INI_SECTION *s = ini_find_section(ini_store, "UI");
    if(s) {
        for(int i = 0; i < s->kv.items; i++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, i);
            const char *key = kv->key;
            const char *val = kv->val;

            if(0 == stricmp(key, "instance")) {
                if(0 == stricmp(val, "text")) {
                    ui->class = UI_CLASS_TEXT;
               }
            }
        }
    }
}

void ui_init(UI *ui, int model, INI_STORE *ini_store) {
    // Clear the ops and ptr to the actual UI
    memset(ui, 0, sizeof(UI));
    // decide which UI needs to be instanced
    ui_apply_ini(ui, ini_store);
    // Instance the UI and have it configure itself
    if(ui->class == UI_CLASS_GUI) {
        // Instance UNK
        UNK *v = (UNK*)malloc(sizeof(UNK));
        // Clear all 
        memset(v, 0, sizeof(UNK));

        // Link through UI
        ui->user = v;
        ui->ops = &unk_ops;

        // Set a target windows default size
        v->target_rect.w = 1120;
        v->target_rect.h = 840;

        // UNK gets to init
        unk_init(v, model, ini_store);
    } else {
        // Instance UTXT
        UTXT *v = (UTXT*)malloc(sizeof(UTXT));
        // Clear all
        memset(ui->user, 0, sizeof(UTXT));
        // Link through UI
        ui->user = v;
        ui->ops = &utxt_ops;

        // TXT gets to init
        utxt_init(v, ini_store);
    }
}

void ui_shutdown(UI *ui) {
    if(ui->class == UI_CLASS_TEXT) {
        utxt_shutdown((UTXT*)ui->user);
    } else {
        unk_shutdown((UNK*)ui->user);
    }
    free(ui->user);
    ui->ops = &null_ops;
}

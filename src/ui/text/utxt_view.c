// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "utxt_lib.h"

const UI_OPS utxt_ops = {
    .disk_read_led     = NULL,
    .disk_write_led    = NULL,
    .process_events    = NULL,
    .speaker_toggle    = NULL,
    .speaker_on_cycles = NULL,
    .render            = NULL,
    .set_runtime       = NULL,
    .set_shadow_flags  = NULL,
    .shutdown          = NULL,
};

int utxt_init(UTXT *v, INI_STORE *ini_store) {
    return A2_ERR;
}

void utxt_shutdown(UTXT *v) {

}

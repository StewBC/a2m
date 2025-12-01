// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

void unk_joy_ptrig(UI *ui, uint64_t cycle) {
    UNK *v = (UNK*)ui->user;
    if(!v->num_controllers) {
        // See if there's a game controller
        for(int i = 0; i < SDL_NumJoysticks() && v->num_controllers < 2; i++) {
            if(!SDL_IsGameController(i)) {
                continue;
            }
            v->game_controller[v->num_controllers] = SDL_GameControllerOpen(i);
            if(v->game_controller[v->num_controllers]) {
                v->num_controllers++;
            }
        }
    }
    v->ptrig_cycle = cycle;
}

uint8_t unk_joy_read_button(UI *ui, int controller_id, int button_id) {
    UNK *v = (UNK*)ui->user;
    return v->button[controller_id][button_id];
}

uint8_t unk_joy_read_axis(UI *ui, int controller_id, int axis_id, uint64_t cycle) {
    UNK *v = (UNK*)ui->user;
    if(!v->num_controllers) {
        return 0xff;
    }
    uint64_t cycle_delta = cycle - v->ptrig_cycle;
    uint8_t val = clamp_u8(cycle_delta * 255 / paddl_normalized, 0, 255);
    return val > v->axis_left[controller_id][axis_id] ? 0x00 : 0x80;
}

// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

void unk_joy_ptrig(UI *ui, uint64_t cycle);
uint8_t unk_joy_read_button(UI *ui, int controller_id, int button_id);
uint8_t unk_joy_read_axis(UI *ui, int controller_id, int axis_id, uint64_t cycle);

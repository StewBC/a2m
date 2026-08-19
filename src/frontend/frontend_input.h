#pragma once

#include "keyboard.h"

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>

enum {
    FRONTEND_INPUT_MAX_ACTIONS = 4,
};

typedef enum frontend_input_action_type {
    FRONTEND_INPUT_ACTION_NONE = 0,
    FRONTEND_INPUT_ACTION_KEY,
} frontend_input_action_type;

typedef struct frontend_input_action {
    frontend_input_action_type type;
    host_key key;
    bool pressed;
} frontend_input_action;

typedef struct frontend_input_chord {
    frontend_input_action actions[FRONTEND_INPUT_MAX_ACTIONS];
    size_t count;
} frontend_input_chord;

typedef struct frontend_input_mapper {
    frontend_input_chord active[SDL_NUM_SCANCODES];
    bool original_del;
} frontend_input_mapper;

void frontend_input_mapper_reset(frontend_input_mapper *mapper);
void frontend_input_mapper_set_original_del(frontend_input_mapper *mapper, bool enabled);
bool frontend_input_is_host_quit_shortcut(const SDL_KeyboardEvent *key);
bool frontend_input_has_option_modifier(const SDL_KeyboardEvent *key);
bool frontend_input_has_shift_modifier(const SDL_KeyboardEvent *key);
size_t frontend_input_map_keyboard_event(
    frontend_input_mapper *mapper,
    const SDL_KeyboardEvent *event,
    frontend_input_action *actions,
    size_t capacity);

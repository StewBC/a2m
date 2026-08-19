#pragma once

#include "audio_buffer.h"

#include <stdbool.h>

/* Opaque SDL audio device state. SDL types are private to platform_audio.c.
   Use platform_audio_create / platform_audio_destroy for lifetime management. */
typedef struct platform_audio platform_audio;

typedef struct platform_audio_desc {
    int requested_rate;
    int requested_channels;
    int requested_callback_samples;
    audio_buffer *buffer;   /* must outlive the returned platform_audio */
} platform_audio_desc;

/* Allocate, open the SDL audio device, and return a platform_audio.
   Calls SDL_InitSubSystem(SDL_INIT_AUDIO) internally so it may safely
   precede platform_init(). Returns NULL on failure. */
platform_audio *platform_audio_create(const platform_audio_desc *desc);

/* Pause, close the SDL audio device, and free the platform_audio.
   Safe to call with NULL. */
void platform_audio_destroy(platform_audio *audio);

/* Start (unpause) audio playback. */
void platform_audio_start(platform_audio *audio);

/* Pause audio playback without closing the device. */
void platform_audio_stop(platform_audio *audio);

int  platform_audio_actual_rate(const platform_audio *audio);
int  platform_audio_actual_channels(const platform_audio *audio);
bool platform_audio_is_open(const platform_audio *audio);

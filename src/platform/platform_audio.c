#include "platform_audio.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

struct platform_audio {
    SDL_AudioDeviceID device_id;
    int actual_rate;
    int actual_channels;
    SDL_AudioFormat actual_format;
    int callback_samples;   /* frames per callback */
    audio_buffer *buf;      /* not owned; interleaved stereo L,R floats */
    float *stereo_tmp;      /* scratch: 2 * (callback_samples + guard) */
    float last_l;           /* underrun fade state */
    float last_r;
    bool is_open;
};

/* Underrun fade: ~1 ms time constant so a missing chunk dies quietly instead
   of a hard step to zero (which is an audible click mid-tone). */
static float platform_audio_underrun_coeff(int sample_rate)
{
    float r;
    if (sample_rate <= 0) {
        return 0.95f;
    }
    /* R ≈ exp(-1 / (0.001 * fs)) for ~1 ms. */
    r = 1.0f - (1.0f / (0.001f * (float)sample_rate));
    if (r < 0.90f) {
        r = 0.90f;
    } else if (r > 0.999f) {
        r = 0.999f;
    }
    return r;
}

static void platform_audio_callback(void *userdata, Uint8 *stream, int len) {
    platform_audio *audio = (platform_audio *)userdata;
    int channels = audio->actual_channels;
    int frames = len / (channels * (int)sizeof(float));
    float *out = (float *)(void *)stream;
    size_t floats_wanted;
    size_t floats_got;
    size_t frames_got;
    float hold_l;
    float hold_r;
    float fade;
    int i;
    int ch;

    if (frames <= 0 || frames > audio->callback_samples) {
        memset(stream, 0, (size_t)len);
        return;
    }

    /* Runtime writes interleaved stereo (2 floats per host frame). */
    floats_wanted = (size_t)frames * 2u;
    floats_got = audio_buffer_read(audio->buf, audio->stereo_tmp, floats_wanted);
    frames_got = floats_got / 2u;

    hold_l = audio->last_l;
    hold_r = audio->last_r;
    fade = platform_audio_underrun_coeff(audio->actual_rate);

    for (i = 0; i < frames; i++) {
        float l;
        float r;

        if ((size_t)i < frames_got) {
            l = audio->stereo_tmp[(size_t)i * 2u];
            r = audio->stereo_tmp[(size_t)i * 2u + 1u];
            hold_l = l;
            hold_r = r;
        } else {
            hold_l *= fade;
            hold_r *= fade;
            if (hold_l > -1.0e-4f && hold_l < 1.0e-4f) {
                hold_l = 0.0f;
            }
            if (hold_r > -1.0e-4f && hold_r < 1.0e-4f) {
                hold_r = 0.0f;
            }
            l = hold_l;
            r = hold_r;
        }

        if (channels <= 1) {
            out[i] = 0.5f * (l + r);
        } else {
            out[i * channels + 0] = l;
            out[i * channels + 1] = r;
            for (ch = 2; ch < channels; ch++) {
                out[i * channels + ch] = 0.5f * (l + r);
            }
        }
    }
    audio->last_l = hold_l;
    audio->last_r = hold_r;
}

platform_audio *platform_audio_create(const platform_audio_desc *desc) {
    platform_audio *audio;
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    int cb_samples;

    if (desc == NULL || desc->buffer == NULL) {
        return NULL;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("platform_audio: SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return NULL;
    }

    audio = calloc(1, sizeof(*audio));
    if (audio == NULL) {
        return NULL;
    }

    audio->buf = desc->buffer;

    cb_samples = desc->requested_callback_samples > 0 ? desc->requested_callback_samples : 512;

    memset(&desired, 0, sizeof(desired));
    desired.freq     = desc->requested_rate > 0 ? desc->requested_rate : 48000;
    desired.format   = AUDIO_F32SYS;
    desired.channels = (Uint8)(desc->requested_channels > 0 ? desc->requested_channels : 2);
    desired.samples  = (Uint16)cb_samples;
    desired.callback = platform_audio_callback;
    desired.userdata = audio;

    audio->device_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (audio->device_id == 0) {
        SDL_Log("platform_audio: SDL_OpenAudioDevice failed: %s", SDL_GetError());
        free(audio);
        return NULL;
    }

    audio->actual_rate     = obtained.freq;
    audio->actual_channels = obtained.channels;
    audio->actual_format   = obtained.format;
    audio->callback_samples = obtained.samples;

    /* Interleaved stereo scratch (guard for minor SDL size quirks). */
    audio->stereo_tmp = calloc((size_t)(obtained.samples + 64) * 2u, sizeof(float));
    if (audio->stereo_tmp == NULL) {
        SDL_CloseAudioDevice(audio->device_id);
        free(audio);
        return NULL;
    }

    audio->is_open = true;

    SDL_Log("platform_audio: opened device id=%u rate=%d channels=%d samples=%d",
        (unsigned)audio->device_id,
        audio->actual_rate,
        audio->actual_channels,
        audio->callback_samples);

    return audio;
}

void platform_audio_destroy(platform_audio *audio) {
    if (audio == NULL) {
        return;
    }

    if (audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 1);
        SDL_CloseAudioDevice(audio->device_id);
        audio->device_id = 0;
    }

    free(audio->stereo_tmp);
    free(audio);
}

void platform_audio_start(platform_audio *audio) {
    if (audio != NULL && audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 0);
    }
}

void platform_audio_stop(platform_audio *audio) {
    if (audio != NULL && audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 1);
    }
}

int platform_audio_actual_rate(const platform_audio *audio) {
    return audio != NULL ? audio->actual_rate : 0;
}

int platform_audio_actual_channels(const platform_audio *audio) {
    return audio != NULL ? audio->actual_channels : 0;
}

bool platform_audio_is_open(const platform_audio *audio) {
    return audio != NULL && audio->is_open;
}

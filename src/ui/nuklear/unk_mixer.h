// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#define SOFT_RING_FRAMES   16384
#define SOFT_RING_MASK     (SOFT_RING_FRAMES - 1)

typedef struct {
    float left;
    float right;
} UNKAUDIOFRAME;

typedef struct {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec have;

    // software ring buffer in explicit stereo frames
    UNKAUDIOFRAME ring[SOFT_RING_FRAMES];
    uint32_t rpos;
    uint32_t wpos;

    // queue control
    uint32_t chunk_frames;
    uint32_t queue_target_bytes;
    float target_latency_ms;
} UNKMIXER;

int unk_mixer_init(UNKMIXER *mixer, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames);
void unk_mixer_shutdown(UNKMIXER *mixer);
void unk_mixer_prime_queue_and_start(UNKMIXER *mixer);
void unk_mixer_push_frame(UNKMIXER *mixer, UNKAUDIOFRAME frame);
void unk_mixer_pump(UNKMIXER *mixer);

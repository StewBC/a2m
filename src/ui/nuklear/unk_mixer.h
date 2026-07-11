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
    uint32_t queue_high_bytes;   // stop accepting more host audio above this
    uint32_t queue_low_bytes;    // pad silence if we fall below this with an empty ring
    float target_latency_ms;
    float bytes_per_frame;
} UNKMIXER;

int unk_mixer_init(UNKMIXER *mixer, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames);
void unk_mixer_shutdown(UNKMIXER *mixer);
void unk_mixer_prime_queue_and_start(UNKMIXER *mixer);
void unk_mixer_push_frame(UNKMIXER *mixer, UNKAUDIOFRAME frame);
void unk_mixer_pump(UNKMIXER *mixer);
uint32_t unk_mixer_queued_bytes(const UNKMIXER *mixer);
uint32_t unk_mixer_ring_frames(const UNKMIXER *mixer);
// True when the host path is full enough that new frames should be discarded
// (chip state may still advance; this is output backpressure only).
int unk_mixer_output_full(const UNKMIXER *mixer);

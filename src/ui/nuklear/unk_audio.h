// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#define SOFT_RING_FRAMES   16384  // 16384 mono frames (~341ms at 48k)
#define SOFT_RING_MASK     (SOFT_RING_FRAMES - 1)

typedef struct {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec have;

    // software ring buffer (mono)
    float ring[SOFT_RING_FRAMES];
    uint32_t rpos;      // frame indices (mono) read and write
    uint32_t wpos;

    // queue control
    uint32_t chunk_frames;    // e.g., 256
    uint32_t target_q_bytes;  // e.g., 40 ms of audio

    // audio clock
    double cpu_hz;              // e.g., 1020484.4 (NTSC)
    double cycles_per_sample;   // = cpu_hz / have.freq
    double cycle_accum;         // cycles accumulated toward next sample

    // speaker state (+1/-1)
    float level;

    // simple filter state (optional)
    float hp_prev_out;
    float prev_sample;
} VIEWSPEAKER;

int  unk_audio_speaker_init(VIEWSPEAKER *sp, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames);
void unk_audio_speaker_on_cycles(UI *ui, uint32_t cycles_executed);
// void unk_audio_speaker_pump(VIEWSPEAKER *sp);
void unk_audio_speaker_shutdown(VIEWSPEAKER *sp);
void unk_audio_speaker_toggle(UI *ui);

void unk_audio_speaker_pump(VIEWSPEAKER *sp);
void unk_spkr_shutdown();

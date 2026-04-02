// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#include "unk_mixer.h"

typedef struct {
    UNKMIXER mixer;

    // audio clock
    double cpu_hz;
    double cycles_per_sample;
    double cycle_accum;
    double mockingboard_cycles_per_render;
    double mockingboard_render_cpu_budget;
    uint32_t mockingboard_render_oversample;

    // speaker source state (+1/-1)
    float speaker_level;
    float mockingboard_gain;
    float mockingboard_mix_scale;
    float mockingboard_filter_alpha;
    float mockingboard_filter_left;
    float mockingboard_filter_right;

    // speaker filter state
    float x_prev;
    float y_prev;

} VIEWAUDIO;

void unk_audio_restart_output(VIEWAUDIO *audio);
int  unk_audio_init(VIEWAUDIO *audio, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames);
void unk_audio_on_cycles(UI *ui, uint32_t cycles_executed);
void unk_audio_shutdown(VIEWAUDIO *audio);
void unk_audio_speaker_toggle(UI *ui);

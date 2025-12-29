// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "common.h"
#include "hardware_lib.h"
#include "unk_audio.h"

static inline uint32_t unk_audio_speaker_ring_count(const VIEWSPEAKER *speaker) {
    return (speaker->wpos - speaker->rpos) & SOFT_RING_MASK;
}

static inline uint32_t unk_audio_speaker_ring_space(const VIEWSPEAKER *speaker) {
    return SOFT_RING_FRAMES - 1 - unk_audio_speaker_ring_count(speaker);
}

static inline void ring_push(VIEWSPEAKER *speaker, float s) {
    if(unk_audio_speaker_ring_space(speaker) == 0) {           // drop oldest if full
        speaker->rpos = (speaker->rpos + 1) & SOFT_RING_MASK;
    }
    speaker->ring[speaker->wpos & SOFT_RING_MASK] = s;
    speaker->wpos = (speaker->wpos + 1) & SOFT_RING_MASK;
}

static inline uint32_t unk_audio_speaker_ring_pop_block(VIEWSPEAKER *speaker, float *dst, uint32_t max_frames) {
    uint32_t have = unk_audio_speaker_ring_count(speaker);
    if(have == 0) {
        return 0;
    }
    if(have > max_frames) {
        have = max_frames;
    }
    uint32_t first = SOFT_RING_FRAMES - (speaker->rpos & SOFT_RING_MASK);
    if(have <= first) {
        memcpy(dst, &speaker->ring[speaker->rpos & SOFT_RING_MASK], have * sizeof(float));
    } else {
        memcpy(dst, &speaker->ring[speaker->rpos & SOFT_RING_MASK], first * sizeof(float));
        memcpy(dst + first, &speaker->ring[0], (have - first) * sizeof(float));
    }
    speaker->rpos = (speaker->rpos + have) & SOFT_RING_MASK;
    return have;
}

static inline float unk_audio_speaker_filter(VIEWSPEAKER *speaker, float x) {
    const float alpha = 0.98f;                              // High pass filter coefficient
    const float beta = 0.98f;                               // Low pass filter coefficient

    float output_previous = speaker->hp_prev_out;
    float sample_previous = speaker->prev_sample;
    float sample_current = x;
    // Calculate a high pass and low pass filtered sample
    float high_pass_result = alpha * (output_previous + sample_current - sample_previous);
    float filter_result = beta * high_pass_result + (1 - beta) * output_previous;
    // Save the current sample of next time
    speaker->prev_sample = x;
    // And make the filtered sample prev and also use as left and right for SDL
    speaker->hp_prev_out = filter_result;
    return filter_result;
}

int unk_audio_speaker_init(VIEWSPEAKER *speaker, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames) {
    memset(speaker, 0, sizeof(*speaker));
    speaker->cpu_hz = (cpu_hz > 0.0) ? cpu_hz : CPU_FREQUENCY;
    speaker->level  = -1.0f;

    SDL_AudioSpec want = {0}, have = {0};
    want.freq = (sample_rate > 0) ? sample_rate : 48000;
    want.format = AUDIO_F32SYS;
    want.channels = (uint8_t)((channels == 1 || channels == 2) ? channels : 2);
    want.samples = 256;
    want.callback = NULL;            // push model
    speaker->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(!speaker->dev) {
        return A2_ERR;
    }
    if(have.format != AUDIO_F32SYS) {
        SDL_CloseAudioDevice(speaker->dev);
        return A2_ERR;
    }
    speaker->have = have;

    speaker->cycles_per_sample = speaker->cpu_hz / (double)have.freq;
    speaker->cycle_accum = 0.0;

    speaker->chunk_frames = (chunk_frames ? chunk_frames : 256);
    double bytes_per_frame = have.channels * sizeof(float);
    double target_ms = (target_latency_ms > 0.0f) ? target_latency_ms : 40.0;
    speaker->target_q_bytes = (uint32_t)((double)have.freq * bytes_per_frame * target_ms / 1000.0);

    SDL_PauseAudioDevice(speaker->dev, 0);
    SDL_PauseAudio(0);
    return A2_OK;
}

// Produce mono audio at exact emulated sample clock and push into ring
void unk_audio_speaker_on_cycles(UI *ui, uint32_t cycles_executed) {
    UNK *v = (UNK *)ui->user;
    VIEWSPEAKER *speaker = &v->viewspeaker;

    speaker->cycle_accum += (double)cycles_executed;
    while(speaker->cycle_accum >= speaker->cycles_per_sample) {
        speaker->cycle_accum -= speaker->cycles_per_sample;

        float x = speaker->level;   // -1 or +1
        float y = unk_audio_speaker_filter(speaker, x);
        ring_push(speaker, y);
        unk_audio_speaker_pump(speaker); // SQW
    }
}

void unk_audio_speaker_shutdown(VIEWSPEAKER *speaker) {
    if(speaker->dev) {
        SDL_ClearQueuedAudio(speaker->dev);
        SDL_CloseAudioDevice(speaker->dev);
        speaker->dev = 0;
    }
}


void unk_audio_speaker_toggle(UI *ui) {
    UNK *v = (UNK *)ui->user;
    VIEWSPEAKER *speaker = &v->viewspeaker;
    speaker->level = -speaker->level;
}

// Feed SDL from ring, but cap queue to target latency
void unk_audio_speaker_pump(VIEWSPEAKER *speaker) {
    uint32_t queued = SDL_GetQueuedAudioSize(speaker->dev);
    if(queued >= speaker->target_q_bytes) {
        return;
    }

    // prepare a small chunk (expand mono -> interleaved stereo/mono)
    uint32_t want_frames = speaker->chunk_frames;
    float mono[4096];
    if(want_frames > 4096) {
        want_frames = 4096;
    }

    uint32_t got = unk_audio_speaker_ring_pop_block(speaker, mono, want_frames);
    if(got == 0) {
        return;
    }

    const int ch = speaker->have.channels;
    if(ch == 1) {
        SDL_QueueAudio(speaker->dev, mono, got * sizeof(float));
    } else {
        static float stereo[2 * 4096];
        for(uint32_t i = 0; i < got; ++i) {
            stereo[2 * i + 0] = mono[i];
            stereo[2 * i + 1] = mono[i];
        }
        SDL_QueueAudio(speaker->dev, stereo, got * 2 * sizeof(float));
    }
}
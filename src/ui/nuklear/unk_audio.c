// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

static inline float dc_block(VIEWAUDIO *audio, float x) {
    const float R = 0.995f;

    float y = x - audio->x_prev + R * audio->y_prev;
    audio->x_prev = x;
    audio->y_prev = y;
    return y;
}

static inline float clamp_audio_sample(float x) {
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

static inline UNKAUDIOFRAME unk_audio_centered_frame(float sample) {
    UNKAUDIOFRAME frame = {
        .left = sample,
        .right = sample,
    };
    return frame;
}

static inline UNKAUDIOFRAME unk_audio_clamp_frame(UNKAUDIOFRAME frame) {
    frame.left = clamp_audio_sample(frame.left);
    frame.right = clamp_audio_sample(frame.right);
    return frame;
}

void unk_audio_restart_output(VIEWAUDIO *audio) {
    unk_mixer_prime_queue_and_start(&audio->mixer);
}

int unk_audio_init(VIEWAUDIO *audio, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames) {
    memset(audio, 0, sizeof(*audio));
    audio->cpu_hz = (cpu_hz > 0.0) ? cpu_hz : CPU_FREQUENCY;
    audio->speaker_level = -1.0f;
    audio->mockingboard_gain = 1.0f;

    if(A2_OK != unk_mixer_init(&audio->mixer, sample_rate, channels, target_latency_ms, chunk_frames)) {
        return A2_ERR;
    }
    audio->cycles_per_sample = audio->cpu_hz / (double)audio->mixer.have.freq;
    audio->cycle_accum = 0.0;
    return A2_OK;
}

// Output sample times are derived from accumulated emulated CPU cycles
// Speaker and Mockingboard sources are sampled here
// Speaker is centered, while Mockingboard is stereo
// Final queue/ring/SDL output handling lives in the mixer module
void unk_audio_on_cycles(UI *ui, uint32_t cycles_executed) {
    UNK *v = (UNK *)ui->user;
    VIEWAUDIO *audio = &v->viewaudio;
    APPLE2 *m = v->rt ? v->rt->m : v->m;

    audio->cycle_accum += (double)cycles_executed;

    uint32_t produced = 0;
    while(audio->cycle_accum >= audio->cycles_per_sample) {
        audio->cycle_accum -= audio->cycles_per_sample;

        float speaker_sample = dc_block(audio, audio->speaker_level);
        UNKAUDIOFRAME mixed = unk_audio_centered_frame(speaker_sample);

        if(m) {
            if(m->mb_slot) {
                MOCKINGBOARD_SAMPLE mockingboard_sample = mockingboard_get_stereo_sample(&m->mockingboard[m->mb_slot]);
                mixed.left += mockingboard_sample.left * audio->mockingboard_gain;
                mixed.right += mockingboard_sample.right * audio->mockingboard_gain;
            }
        }

        unk_mixer_push_frame(&audio->mixer, unk_audio_clamp_frame(mixed));
        produced++;
    }

    if(produced) {
        unk_mixer_pump(&audio->mixer);
    }
}

void unk_audio_speaker_toggle(UI *ui) {
    UNK *v = (UNK *)ui->user;
    VIEWAUDIO *audio = &v->viewaudio;
    audio->speaker_level = -audio->speaker_level;
}

void unk_audio_shutdown(VIEWAUDIO *audio) {
    unk_mixer_shutdown(&audio->mixer);
}

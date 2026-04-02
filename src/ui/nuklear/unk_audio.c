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

static inline float unk_audio_lowpass_step(float x, float *state, float alpha) {
    float y = alpha * x + (1.0f - alpha) * (*state);
    *state = y;
    return y;
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

static inline void unk_audio_accumulate_mockingboard_subframe(VIEWAUDIO *audio, MOCKINGBOARD *mb, UNKAUDIOFRAME *frame) {
    audio->mockingboard_render_cpu_budget += audio->mockingboard_cycles_per_render;
    uint32_t render_cpu_cycles = (uint32_t)audio->mockingboard_render_cpu_budget;
    audio->mockingboard_render_cpu_budget -= (double)render_cpu_cycles;

    MOCKINGBOARD_SAMPLE subframe = mockingboard_render_audio_sample(mb, render_cpu_cycles);
    frame->left += subframe.left;
    frame->right += subframe.right;
}

static inline int unk_audio_filter_is_settled(const VIEWAUDIO *audio) {
    const float epsilon = 1.0e-6f;
    return fabsf(audio->mockingboard_filter_left) < epsilon &&
           fabsf(audio->mockingboard_filter_right) < epsilon;
}

static inline UNKAUDIOFRAME unk_audio_render_mockingboard_frame(VIEWAUDIO *audio, MOCKINGBOARD *mb) {
    UNKAUDIOFRAME mockingboard_frame = {0};

    if(!mb) {
        return mockingboard_frame;
    }

    if(mockingboard_is_audibly_idle(mb)) {
        if(unk_audio_filter_is_settled(audio)) {
            return mockingboard_frame;
        }
        mockingboard_frame.left = unk_audio_lowpass_step(0.0f, &audio->mockingboard_filter_left, audio->mockingboard_filter_alpha);
        mockingboard_frame.right = unk_audio_lowpass_step(0.0f, &audio->mockingboard_filter_right, audio->mockingboard_filter_alpha);
        return mockingboard_frame;
    }

    if(audio->mockingboard_render_oversample == 4) {
        unk_audio_accumulate_mockingboard_subframe(audio, mb, &mockingboard_frame);
        unk_audio_accumulate_mockingboard_subframe(audio, mb, &mockingboard_frame);
        unk_audio_accumulate_mockingboard_subframe(audio, mb, &mockingboard_frame);
        unk_audio_accumulate_mockingboard_subframe(audio, mb, &mockingboard_frame);
    } else {
        for(uint32_t i = 0; i < audio->mockingboard_render_oversample; i++) {
            unk_audio_accumulate_mockingboard_subframe(audio, mb, &mockingboard_frame);
        }
    }

    mockingboard_frame.left *= audio->mockingboard_mix_scale;
    mockingboard_frame.right *= audio->mockingboard_mix_scale;
    mockingboard_frame.left = unk_audio_lowpass_step(mockingboard_frame.left, &audio->mockingboard_filter_left, audio->mockingboard_filter_alpha);
    mockingboard_frame.right = unk_audio_lowpass_step(mockingboard_frame.right, &audio->mockingboard_filter_right, audio->mockingboard_filter_alpha);
    return mockingboard_frame;
}

void unk_audio_restart_output(VIEWAUDIO *audio) {
    unk_mixer_prime_queue_and_start(&audio->mixer);
}

int unk_audio_init(VIEWAUDIO *audio, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames) {
    memset(audio, 0, sizeof(*audio));
    audio->cpu_hz = (cpu_hz > 0.0) ? cpu_hz : CPU_FREQUENCY;
    audio->speaker_level = -1.0f;
    audio->mockingboard_gain = 0.985f;

    if(A2_OK != unk_mixer_init(&audio->mixer, sample_rate, channels, target_latency_ms, chunk_frames)) {
        return A2_ERR;
    }
    audio->cycles_per_sample = audio->cpu_hz / (double)audio->mixer.have.freq;
    audio->mockingboard_render_oversample = 4;
    audio->mockingboard_cycles_per_render = audio->cycles_per_sample / (double)audio->mockingboard_render_oversample;
    audio->mockingboard_render_cpu_budget = 0.0;
    audio->mockingboard_mix_scale = audio->mockingboard_gain / (float)audio->mockingboard_render_oversample;
    {
        const double cutoff_hz = 48000.0;
        const double dt = 1.0 / (double)audio->mixer.have.freq;
        const double rc = 1.0 / (2.0 * M_PI * cutoff_hz);
        audio->mockingboard_filter_alpha = (float)(dt / (rc + dt));
    }
    audio->cycle_accum = 0.0;
    return A2_OK;
}

// Output sample times are derived from accumulated emulated CPU cycles.
// This file is the host-audio reconstruction/output stage for the emulator:
// it takes already-emulated speaker and Mockingboard state and turns that into
// host PCM. Fidelity work belongs here and in related mixer code, not in the
// bus/register semantics of the hardware layer.
// Speaker and Mockingboard sources are sampled here.
// Speaker is centered, while Mockingboard is stereo.
// Final queue/ring/SDL output handling lives in the mixer module.
void unk_audio_on_cycles(UI *ui, uint32_t cycles_executed) {
    UNK *v = (UNK *)ui->user;
    VIEWAUDIO *audio = &v->viewaudio;
    APPLE2 *m = v->rt ? v->rt->m : v->m;
    MOCKINGBOARD *mb = (m && m->mb_slot) ? &m->mockingboard[m->mb_slot] : NULL;
    int mockingboard_silent = mb && mockingboard_is_audibly_idle(mb) && unk_audio_filter_is_settled(audio);

    audio->cycle_accum += (double)cycles_executed;

    uint32_t produced = 0;
    while(audio->cycle_accum >= audio->cycles_per_sample) {
        audio->cycle_accum -= audio->cycles_per_sample;

        float speaker_sample = dc_block(audio, audio->speaker_level);
        UNKAUDIOFRAME mixed = unk_audio_centered_frame(speaker_sample);
        UNKAUDIOFRAME mockingboard_frame = {0};

        if(mb && !mockingboard_silent) {
            mockingboard_frame = unk_audio_render_mockingboard_frame(audio, mb);
            mixed.left += mockingboard_frame.left;
            mixed.right += mockingboard_frame.right;
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

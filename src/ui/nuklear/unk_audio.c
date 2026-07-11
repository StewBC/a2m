// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

// One-pole DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]
// R ~ 0.995 at 48 kHz puts the high-pass knee near a few tens of Hz.
static inline float unk_audio_dc_block_step(float x, float *x_prev, float *y_prev, float R) {
    float y = x - (*x_prev) + R * (*y_prev);
    *x_prev = x;
    *y_prev = y;
    return y;
}

static inline float dc_block(VIEWAUDIO *audio, float x) {
    return unk_audio_dc_block_step(x, &audio->x_prev, &audio->y_prev, 0.995f);
}

static inline float clamp_audio_sample(float x) {
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

// Linear below thr, then soft-knee asymptote toward ±1 so speaker+MB peaks
// compress instead of hard-clipping.
static inline float soft_clip_sample(float x) {
    const float thr = 0.90f;
    float ax = fabsf(x);
    float sign;
    float over;
    float room;
    float y;

    if(ax <= thr) {
        return x;
    }

    sign = (x < 0.0f) ? -1.0f : 1.0f;
    over = ax - thr;
    room = 1.0f - thr;
    y = thr + room * (over / (over + room));
    return sign * y;
}

static inline float unk_audio_lowpass_step(float x, float *state, float alpha) {
    float y = alpha * x + (1.0f - alpha) * (*state);
    *state = y;
    return y;
}

// Two cascaded one-poles ≈ gentle 2nd-order low-pass for post-decimation anti-alias.
static inline float unk_audio_lowpass2_step(float x, float *state1, float *state2, float alpha) {
    float y1 = unk_audio_lowpass_step(x, state1, alpha);
    return unk_audio_lowpass_step(y1, state2, alpha);
}

static inline UNKAUDIOFRAME unk_audio_mockingboard_dc_block(VIEWAUDIO *audio, UNKAUDIOFRAME frame) {
    const float R = 0.995f;
    frame.left = unk_audio_dc_block_step(frame.left, &audio->mockingboard_dc_x_left,
                                         &audio->mockingboard_dc_y_left, R);
    frame.right = unk_audio_dc_block_step(frame.right, &audio->mockingboard_dc_x_right,
                                          &audio->mockingboard_dc_y_right, R);
    return frame;
}

static inline UNKAUDIOFRAME unk_audio_mockingboard_lowpass(VIEWAUDIO *audio, UNKAUDIOFRAME frame) {
    frame.left = unk_audio_lowpass2_step(frame.left, &audio->mockingboard_filter_left,
                                         &audio->mockingboard_filter2_left, audio->mockingboard_filter_alpha);
    frame.right = unk_audio_lowpass2_step(frame.right, &audio->mockingboard_filter_right,
                                          &audio->mockingboard_filter2_right, audio->mockingboard_filter_alpha);
    return frame;
}

static inline UNKAUDIOFRAME unk_audio_centered_frame(float sample) {
    UNKAUDIOFRAME frame = {
        .left = sample,
        .right = sample,
    };
    return frame;
}

static inline UNKAUDIOFRAME unk_audio_limit_frame(UNKAUDIOFRAME frame) {
    frame.left = clamp_audio_sample(soft_clip_sample(frame.left));
    frame.right = clamp_audio_sample(soft_clip_sample(frame.right));
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
           fabsf(audio->mockingboard_filter_right) < epsilon &&
           fabsf(audio->mockingboard_filter2_left) < epsilon &&
           fabsf(audio->mockingboard_filter2_right) < epsilon &&
           fabsf(audio->mockingboard_dc_x_left) < epsilon &&
           fabsf(audio->mockingboard_dc_y_left) < epsilon &&
           fabsf(audio->mockingboard_dc_x_right) < epsilon &&
           fabsf(audio->mockingboard_dc_y_right) < epsilon;
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
        // Drain LPF + DC state with silence so stop edges do not leave residual offset.
        mockingboard_frame = unk_audio_mockingboard_lowpass(audio, mockingboard_frame);
        return unk_audio_mockingboard_dc_block(audio, mockingboard_frame);
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
    // ~14 kHz 2-pole LPF after box decimation; cuts harsh square-wave aliases.
    mockingboard_frame = unk_audio_mockingboard_lowpass(audio, mockingboard_frame);
    // AY channel levels are unipolar; center them before summing with the speaker.
    return unk_audio_mockingboard_dc_block(audio, mockingboard_frame);
}

static void unk_audio_sync_render_rates(VIEWAUDIO *audio) {
    audio->mockingboard_cycles_per_render =
        audio->cycles_per_sample / (double)audio->mockingboard_render_oversample;
}

void unk_audio_restart_output(VIEWAUDIO *audio) {
    unk_mixer_prime_queue_and_start(&audio->mixer);
    audio->cycle_accum = 0.0;
    audio->mockingboard_render_cpu_budget = 0.0;
    audio->drift_sample_counter = 0;
    audio->cycles_per_sample = audio->cycles_per_sample_base;
    unk_audio_sync_render_rates(audio);
}

// Nudge host sample timing so long-run SDL queue depth stays near the target.
// Only used at 1x; turbo uses backpressure/drop instead.
static void unk_audio_adjust_clock_drift(VIEWAUDIO *audio) {
    uint32_t queued;
    uint32_t target;
    double ratio;
    double cps;
    double lo;
    double hi;

    if(!audio->cycles_per_sample_base || !audio->mixer.queue_target_bytes) {
        return;
    }

    audio->drift_sample_counter++;
    // A few ms between adjustments keeps this from chirping the pitch.
    if(audio->drift_sample_counter < 256u) {
        return;
    }
    audio->drift_sample_counter = 0;

    queued = unk_mixer_queued_bytes(&audio->mixer);
    target = audio->mixer.queue_target_bytes;
    if(!target) {
        return;
    }

    ratio = (double)queued / (double)target;
    cps = audio->cycles_per_sample;

    // Queue high → produce slightly fewer host samples per CPU cycle (raise cps).
    // Queue low  → produce slightly more (lower cps).
    if(ratio > 1.15) {
        cps *= 1.0004;
    } else if(ratio > 1.05) {
        cps *= 1.00015;
    } else if(ratio < 0.35) {
        cps *= 0.9996;
    } else if(ratio < 0.60) {
        cps *= 0.99985;
    } else {
        // Ease back toward nominal when near target.
        cps = cps * 0.999 + audio->cycles_per_sample_base * 0.001;
    }

    lo = audio->cycles_per_sample_base * 0.995;
    hi = audio->cycles_per_sample_base * 1.005;
    if(cps < lo) {
        cps = lo;
    } else if(cps > hi) {
        cps = hi;
    }

    audio->cycles_per_sample = cps;
    unk_audio_sync_render_rates(audio);
}

int unk_audio_init(VIEWAUDIO *audio, double cpu_hz, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames) {
    memset(audio, 0, sizeof(*audio));
    audio->cpu_hz = (cpu_hz > 0.0) ? cpu_hz : CPU_FREQUENCY;
    audio->speaker_level = -1.0f;
    // Shared headroom: speaker and MB must coexist without slamming ±1.
    audio->speaker_gain = 0.40f;
    audio->mockingboard_gain = 0.70f;

    if(A2_OK != unk_mixer_init(&audio->mixer, sample_rate, channels, target_latency_ms, chunk_frames)) {
        return A2_ERR;
    }
    audio->cycles_per_sample_base = audio->cpu_hz / (double)audio->mixer.have.freq;
    audio->cycles_per_sample = audio->cycles_per_sample_base;
    audio->mockingboard_render_oversample = 4;
    audio->mockingboard_render_cpu_budget = 0.0;
    audio->mockingboard_mix_scale = audio->mockingboard_gain / (float)audio->mockingboard_render_oversample;
    unk_audio_sync_render_rates(audio);
    {
        // Anti-alias / tone-smoothing low-pass below Nyquist (not a no-op at fs).
        const double cutoff_hz = 14000.0;
        const double dt = 1.0 / (double)audio->mixer.have.freq;
        const double rc = 1.0 / (2.0 * M_PI * cutoff_hz);
        audio->mockingboard_filter_alpha = (float)(dt / (rc + dt));
    }
    audio->cycle_accum = 0.0;
    audio->was_turbo = 0;
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
//
// Turbo / underrun / drift policy:
// - Chip state always advances with emulated time; frames are always generated.
// - The soft ring drops oldest frames under turbo flood so host audio tracks the
//   latest stream instead of growing unbounded latency.
// - Entering/leaving turbo resyncs the SDL queue so 1x does not inherit backlog.
// - At 1x, gentle drift correction keeps long-run queue depth near the target.
// - If the ring is empty and the device is starving, the mixer pads silence.
void unk_audio_on_cycles(UI *ui, uint32_t cycles_executed) {
    UNK *v = (UNK *)ui->user;
    VIEWAUDIO *audio = &v->viewaudio;
    APPLE2 *m = v->rt ? v->rt->m : v->m;
    MOCKINGBOARD *mb = (m && m->mb_slot) ? &m->mockingboard[m->mb_slot] : NULL;
    int mockingboard_silent = mb && mockingboard_is_audibly_idle(mb) && unk_audio_filter_is_settled(audio);
    double turbo = (v->rt) ? v->rt->turbo_active : 1.0;
    // turbo_active <= 0 means max-speed; >1 means explicit multiplier.
    int turbo_mode = (turbo <= 0.0) || (turbo > 1.01);
    uint32_t produced = 0;

    if((uint8_t)turbo_mode != audio->was_turbo) {
        // Entering or leaving turbo: drop queued host audio so the next mode starts clean.
        unk_audio_restart_output(audio);
        audio->was_turbo = (uint8_t)turbo_mode;
    }

    audio->cycle_accum += (double)cycles_executed;

    while(audio->cycle_accum >= audio->cycles_per_sample) {
        float speaker_sample;
        UNKAUDIOFRAME mixed;
        UNKAUDIOFRAME mockingboard_frame = {0};

        audio->cycle_accum -= audio->cycles_per_sample;

        speaker_sample = dc_block(audio, audio->speaker_level) * audio->speaker_gain;
        mixed = unk_audio_centered_frame(speaker_sample);

        if(mb && !mockingboard_silent) {
            mockingboard_frame = unk_audio_render_mockingboard_frame(audio, mb);
            mixed.left += mockingboard_frame.left;
            mixed.right += mockingboard_frame.right;
        }

        // Ring drop-oldest is the turbo backpressure; never freeze on stale audio.
        unk_mixer_push_frame(&audio->mixer, unk_audio_limit_frame(mixed));
        produced++;
    }

    if(produced) {
        unk_mixer_pump(&audio->mixer);
        if(!turbo_mode) {
            unk_audio_adjust_clock_drift(audio);
        }
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

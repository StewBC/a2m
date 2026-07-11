// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

static inline uint32_t unk_mixer_ring_count(const UNKMIXER *mixer) {
    return (mixer->wpos - mixer->rpos) & SOFT_RING_MASK;
}

static inline uint32_t unk_mixer_ring_space(const UNKMIXER *mixer) {
    return SOFT_RING_FRAMES - 1 - unk_mixer_ring_count(mixer);
}

static inline uint32_t unk_mixer_ring_pop_frames(UNKMIXER *mixer, UNKAUDIOFRAME *dst, uint32_t max_frames) {
    uint32_t have = unk_mixer_ring_count(mixer);
    if(have == 0) {
        return 0;
    }
    if(have > max_frames) {
        have = max_frames;
    }
    uint32_t first = SOFT_RING_FRAMES - (mixer->rpos & SOFT_RING_MASK);
    if(have <= first) {
        memcpy(dst, &mixer->ring[mixer->rpos & SOFT_RING_MASK], have * sizeof(UNKAUDIOFRAME));
    } else {
        memcpy(dst, &mixer->ring[mixer->rpos & SOFT_RING_MASK], first * sizeof(UNKAUDIOFRAME));
        memcpy(dst + first, &mixer->ring[0], (have - first) * sizeof(UNKAUDIOFRAME));
    }
    mixer->rpos = (mixer->rpos + have) & SOFT_RING_MASK;
    return have;
}

uint32_t unk_mixer_queued_bytes(const UNKMIXER *mixer) {
    if(!mixer || !mixer->dev) {
        return 0;
    }
    return SDL_GetQueuedAudioSize(mixer->dev);
}

uint32_t unk_mixer_ring_frames(const UNKMIXER *mixer) {
    if(!mixer) {
        return 0;
    }
    return unk_mixer_ring_count(mixer);
}

int unk_mixer_output_full(const UNKMIXER *mixer) {
    uint32_t queued;
    uint32_t ring_frames;

    if(!mixer || !mixer->dev) {
        return 1;
    }

    queued = SDL_GetQueuedAudioSize(mixer->dev);
    if(queued >= mixer->queue_high_bytes) {
        return 1;
    }

    // Leave a little ring headroom so pump can still batch chunks.
    ring_frames = unk_mixer_ring_count(mixer);
    if(ring_frames >= (SOFT_RING_FRAMES * 3u) / 4u) {
        return 1;
    }

    return 0;
}

void unk_mixer_push_frame(UNKMIXER *mixer, UNKAUDIOFRAME frame) {
    // FIFO overflow guard: drop oldest so latency stays bounded under turbo floods.
    if(unk_mixer_ring_space(mixer) == 0) {
        mixer->rpos = (mixer->rpos + 1) & SOFT_RING_MASK;
    }
    mixer->ring[mixer->wpos & SOFT_RING_MASK] = frame;
    mixer->wpos = (mixer->wpos + 1) & SOFT_RING_MASK;
}

void unk_mixer_prime_queue_and_start(UNKMIXER *mixer) {
    if(!mixer || !mixer->dev) {
        return;
    }

    SDL_PauseAudioDevice(mixer->dev, 1);
    SDL_ClearQueuedAudio(mixer->dev);
    mixer->rpos = 0;
    mixer->wpos = 0;

    SDL_PauseAudioDevice(mixer->dev, 0);
}

int unk_mixer_init(UNKMIXER *mixer, int sample_rate, int channels, float target_latency_ms, uint32_t chunk_frames) {
    memset(mixer, 0, sizeof(*mixer));

    SDL_AudioSpec want = {0}, have = {0};
    want.freq = (sample_rate > 0) ? sample_rate : 48000;
    want.format = AUDIO_F32SYS;
    want.channels = (uint8_t)((channels == 1 || channels == 2) ? channels : 2);
    want.samples = 256;
    want.callback = NULL;
    mixer->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(!mixer->dev) {
        return A2_ERR;
    }
    if(have.format != AUDIO_F32SYS) {
        SDL_CloseAudioDevice(mixer->dev);
        mixer->dev = 0;
        return A2_ERR;
    }
    mixer->have = have;
    mixer->chunk_frames = (chunk_frames ? chunk_frames : 256);

    mixer->bytes_per_frame = (float)(have.channels * sizeof(float));
    mixer->target_latency_ms = (target_latency_ms > 0.0f) ? target_latency_ms : 40.0f;
    mixer->queue_target_bytes = (uint32_t)((double)have.freq * (double)mixer->bytes_per_frame *
                                           (double)mixer->target_latency_ms / 1000.0);
    // High water absorbs short bursts; above this producers should discard frames.
    mixer->queue_high_bytes = mixer->queue_target_bytes + mixer->queue_target_bytes / 2u;
    if(mixer->queue_high_bytes < mixer->queue_target_bytes + 1u) {
        mixer->queue_high_bytes = mixer->queue_target_bytes + 1u;
    }
    // Low water: if the device is starving and the ring is empty, pad silence.
    mixer->queue_low_bytes = mixer->queue_target_bytes / 4u;
    if(mixer->queue_low_bytes < (uint32_t)mixer->bytes_per_frame * 64u) {
        mixer->queue_low_bytes = (uint32_t)mixer->bytes_per_frame * 64u;
    }

    unk_mixer_prime_queue_and_start(mixer);
    return A2_OK;
}

void unk_mixer_shutdown(UNKMIXER *mixer) {
    if(mixer->dev) {
        SDL_ClearQueuedAudio(mixer->dev);
        SDL_CloseAudioDevice(mixer->dev);
        mixer->dev = 0;
    }
}

static void unk_mixer_queue_silence(UNKMIXER *mixer, uint32_t frames) {
    float zeros[4096];
    uint32_t max_frames;
    uint32_t bytes;

    if(!mixer || !mixer->dev || !frames) {
        return;
    }

    max_frames = (uint32_t)(sizeof(zeros) / (sizeof(float) * (size_t)mixer->have.channels));
    if(max_frames == 0) {
        return;
    }

    memset(zeros, 0, sizeof(zeros));
    while(frames) {
        uint32_t n = frames < max_frames ? frames : max_frames;
        bytes = n * (uint32_t)mixer->have.channels * (uint32_t)sizeof(float);
        if(SDL_QueueAudio(mixer->dev, zeros, bytes) < 0) {
            return;
        }
        frames -= n;
    }
}

void unk_mixer_pump(UNKMIXER *mixer) {
    const int ch = mixer->have.channels;

    UNKAUDIOFRAME stereo[4096];
    float mono[4096];

    uint32_t want_frames = mixer->chunk_frames;
    if(want_frames > 4096) {
        want_frames = 4096;
    }

    for(;;) {
        uint32_t queued = SDL_GetQueuedAudioSize(mixer->dev);
        if(queued >= mixer->queue_target_bytes) {
            return;
        }

        uint32_t got = unk_mixer_ring_pop_frames(mixer, stereo, want_frames);
        if(got == 0) {
            // Underrun pad: keep a little silence in the device queue so SDL does not
            // glitch when emulation stalls (debug stop, hitch) with an empty ring.
            if(queued < mixer->queue_low_bytes) {
                uint32_t need_bytes = mixer->queue_low_bytes - queued;
                uint32_t bpf = (uint32_t)mixer->bytes_per_frame;
                uint32_t pad_frames = bpf ? (need_bytes + bpf - 1u) / bpf : 0;
                if(pad_frames > want_frames) {
                    pad_frames = want_frames;
                }
                unk_mixer_queue_silence(mixer, pad_frames);
            }
            return;
        }

        int gave;
        if(ch == 1) {
            for(uint32_t i = 0; i < got; ++i) {
                mono[i] = 0.5f * (stereo[i].left + stereo[i].right);
            }
            gave = SDL_QueueAudio(mixer->dev, mono, got * sizeof(float));
        } else {
            gave = SDL_QueueAudio(mixer->dev, stereo, got * sizeof(UNKAUDIOFRAME));
        }
        if(gave < 0) {
            return;
        }
    }
}

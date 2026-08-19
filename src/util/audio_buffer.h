#pragma once

/* C99-compatible header. The struct is defined only in audio_buffer.c (C11).
   Callers hold and pass audio_buffer * pointers; they do not embed the struct. */

#include <stddef.h>
#include <stdint.h>

typedef struct audio_buffer audio_buffer;

/* Allocate and initialise a ring buffer for |capacity_samples| float slots.
   Product host audio uses interleaved stereo (L,R per frame), so capacity is
   typically frames*2. Capacity is rounded up to the next power of two.
   Returns NULL on allocation failure. */
audio_buffer *audio_buffer_create(size_t capacity_samples);

/* Stop and free a buffer. Safe to call with NULL. */
void audio_buffer_destroy(audio_buffer *buf);

/* Clear all samples and reset counters. Must not be called while both
   producer and consumer are running concurrently. */
void audio_buffer_reset(audio_buffer *buf);

/* Producer (runtime thread): write up to |count| float slots from |samples|
   (interleaved L,R for stereo). Returns slots written. If not all fit, the
   surplus is dropped and the overrun counter is incremented once. */
size_t audio_buffer_write(audio_buffer *buf, const float *samples, size_t count);

/* Consumer (SDL callback thread): read up to |count| float slots into |out|.
   Returns slots read. If fewer than |count| are available, the underrun
   counter is incremented once; the caller pads/fades the rest. Non-blocking;
   safe to call from the SDL audio callback. */
size_t audio_buffer_read(audio_buffer *buf, float *out, size_t count);

size_t audio_buffer_available_read(const audio_buffer *buf);
size_t audio_buffer_available_write(const audio_buffer *buf);
size_t audio_buffer_capacity(const audio_buffer *buf);

uint64_t audio_buffer_underrun_count(const audio_buffer *buf);
uint64_t audio_buffer_overrun_count(const audio_buffer *buf);

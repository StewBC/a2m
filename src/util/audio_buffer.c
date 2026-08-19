/* audio_buffer.c — compiled at C11 (see CMakeLists.txt per-file property).
   Uses C11 _Atomic for lock-free SPSC ring buffer.
   The public header (audio_buffer.h) is C99-compatible. */

#include "audio_buffer.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* SPSC ring buffer using unbounded atomic indices (mod capacity, power-of-two).
   Producer owns write_pos; consumer owns read_pos.
   - filled  = write_pos - read_pos
   - available_write = capacity - filled
   - available_read  = filled
   Index into data[] = pos & mask  (mask = capacity - 1). */
struct audio_buffer {
    float *data;
    uint32_t capacity;    /* power-of-two number of float slots */
    uint32_t mask;        /* capacity - 1 */
    _Atomic uint32_t write_pos;
    _Atomic uint32_t read_pos;
    _Atomic uint64_t underruns;
    _Atomic uint64_t overruns;
};

static uint32_t next_power_of_two(uint32_t n) {
    if (n == 0) {
        return 1;
    }
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

audio_buffer *audio_buffer_create(size_t capacity_samples) {
    audio_buffer *buf;
    uint32_t cap;

    if (capacity_samples == 0 || capacity_samples > (1u << 30)) {
        return NULL;
    }

    cap = next_power_of_two((uint32_t)capacity_samples);

    buf = calloc(1, sizeof(*buf));
    if (buf == NULL) {
        return NULL;
    }

    buf->data = calloc(cap, sizeof(float));
    if (buf->data == NULL) {
        free(buf);
        return NULL;
    }

    buf->capacity = cap;
    buf->mask = cap - 1u;
    atomic_init(&buf->write_pos, 0u);
    atomic_init(&buf->read_pos, 0u);
    atomic_init(&buf->underruns, 0u);
    atomic_init(&buf->overruns, 0u);
    return buf;
}

void audio_buffer_destroy(audio_buffer *buf) {
    if (buf == NULL) {
        return;
    }
    free(buf->data);
    free(buf);
}

void audio_buffer_reset(audio_buffer *buf) {
    if (buf == NULL) {
        return;
    }
    atomic_store_explicit(&buf->write_pos, 0u, memory_order_relaxed);
    atomic_store_explicit(&buf->read_pos,  0u, memory_order_relaxed);
    memset(buf->data, 0, buf->capacity * sizeof(float));
}

size_t audio_buffer_write(audio_buffer *buf, const float *samples, size_t count) {
    uint32_t write, read, filled, avail, i;

    if (buf == NULL || samples == NULL || count == 0) {
        return 0;
    }

    write = atomic_load_explicit(&buf->write_pos, memory_order_relaxed);
    read  = atomic_load_explicit(&buf->read_pos,  memory_order_acquire);

    filled = write - read;                    /* wraps safely in uint32 */
    avail  = buf->capacity - filled;

    if (avail == 0) {
        atomic_fetch_add_explicit(&buf->overruns, 1u, memory_order_relaxed);
        return 0;
    }

    if (count > avail) {
        atomic_fetch_add_explicit(&buf->overruns, 1u, memory_order_relaxed);
        count = avail;
    }

    for (i = 0; i < (uint32_t)count; i++) {
        buf->data[(write + i) & buf->mask] = samples[i];
    }

    atomic_store_explicit(&buf->write_pos, write + (uint32_t)count, memory_order_release);
    return count;
}

size_t audio_buffer_read(audio_buffer *buf, float *out, size_t count) {
    uint32_t read, write, filled, actual, i;

    if (buf == NULL || out == NULL || count == 0) {
        return 0;
    }

    read  = atomic_load_explicit(&buf->read_pos,  memory_order_relaxed);
    write = atomic_load_explicit(&buf->write_pos, memory_order_acquire);

    filled = write - read;

    if (filled < (uint32_t)count) {
        if (filled < (uint32_t)count) {
            atomic_fetch_add_explicit(&buf->underruns, 1u, memory_order_relaxed);
        }
        actual = filled;
    } else {
        actual = (uint32_t)count;
    }

    for (i = 0; i < actual; i++) {
        out[i] = buf->data[(read + i) & buf->mask];
    }

    atomic_store_explicit(&buf->read_pos, read + actual, memory_order_release);
    return actual;
}

size_t audio_buffer_available_read(const audio_buffer *buf) {
    uint32_t write, read;

    if (buf == NULL) {
        return 0;
    }

    write = atomic_load_explicit(&buf->write_pos, memory_order_acquire);
    read  = atomic_load_explicit(&buf->read_pos,  memory_order_relaxed);
    return (size_t)(write - read);
}

size_t audio_buffer_available_write(const audio_buffer *buf) {
    uint32_t write, read, filled;

    if (buf == NULL) {
        return 0;
    }

    write  = atomic_load_explicit(&buf->write_pos, memory_order_relaxed);
    read   = atomic_load_explicit(&buf->read_pos,  memory_order_acquire);
    filled = write - read;
    return (size_t)(buf->capacity - filled);
}

size_t audio_buffer_capacity(const audio_buffer *buf) {
    if (buf == NULL) {
        return 0;
    }
    return buf->capacity;
}

uint64_t audio_buffer_underrun_count(const audio_buffer *buf) {
    if (buf == NULL) {
        return 0;
    }
    return atomic_load_explicit(&buf->underruns, memory_order_relaxed);
}

uint64_t audio_buffer_overrun_count(const audio_buffer *buf) {
    if (buf == NULL) {
        return 0;
    }
    return atomic_load_explicit(&buf->overruns, memory_order_relaxed);
}

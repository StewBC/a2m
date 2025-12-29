// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#pragma once

#if defined(_WIN32)

static inline uint64_t perf_counter(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)t.QuadPart;
}

static inline uint64_t perf_frequency(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (uint64_t)f.QuadPart;
}

static inline uint64_t perf_now_ns(void) {
    // Convert QPC ticks to ns (avoid double)
    uint64_t c = perf_counter();
    uint64_t f = perf_frequency();
    // (c * 1e9) / f with reduced overflow risk:
    uint64_t s = c / f;
    uint64_t r = c % f;
    return s * 1000000000ull + (r * 1000000000ull) / f;
}

static inline void perf_sleep(uint32_t ms) {
    Sleep(ms);
}

#elif defined(__APPLE__) // macOS

#include <time.h>
#include <mach/mach_time.h>

static inline uint64_t perf_counter(void) {
  return mach_continuous_time(); // or mach_absolute_time()
}

static inline uint64_t perf_frequency(void) {
  // ticks per second = 1e9 * denom / numer
  static mach_timebase_info_data_t tb;
  static uint64_t freq = 0;
  if (!freq) {
    mach_timebase_info(&tb);
    // Convert: ns = ticks * numer / denom  => ticks/sec = 1e9 * denom / numer
    freq = (1000000000ull * (uint64_t)tb.denom) / (uint64_t)tb.numer;
  }
  return freq;
}

static inline uint64_t perf_now_ns(void) {
  static mach_timebase_info_data_t tb;
  static int inited = 0;
  if (!inited) { mach_timebase_info(&tb); inited = 1; }
  uint64_t t = mach_continuous_time();
  // ns = t * numer / denom
  return (t * (uint64_t)tb.numer) / (uint64_t)tb.denom;
}

static inline void perf_sleep(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

#else // Linux, etc.

#include <time.h>
static inline uint64_t perf_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline uint64_t perf_counter(void) {
    return perf_now_ns();
}

static inline uint64_t perf_frequency(void) {
    return 1000000000ull;
}

static inline void perf_sleep(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

#endif

static inline double perf_seconds(uint64_t start, uint64_t end) {
#if defined(_WIN32)
    return (double)(end - start) / (double)perf_frequency();
#else
    return (double)(end - start) / 1e9;
#endif
}

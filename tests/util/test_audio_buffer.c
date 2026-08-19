#include "audio_buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_size(const char *name, size_t expected, size_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %zu, got %zu\n", name, expected, actual);
        exit(1);
    }
}

static void expect_u64(const char *name, uint64_t expected, uint64_t actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %llu, got %llu\n", name,
            (unsigned long long)expected, (unsigned long long)actual);
        exit(1);
    }
}

static void expect_float(const char *name, float expected, float actual) {
    if (expected != actual) {
        fprintf(stderr, "FAIL: %s: expected %f, got %f\n", name,
            (double)expected, (double)actual);
        exit(1);
    }
}

/* 1. Init/shutdown succeeds for a valid capacity. */
static void test_create_destroy(void) {
    audio_buffer *buf = audio_buffer_create(256);
    if (buf == NULL) {
        fail("create_destroy: audio_buffer_create returned NULL");
    }
    if (audio_buffer_capacity(buf) < 256) {
        fail("create_destroy: capacity too small");
    }
    audio_buffer_destroy(buf);

    /* NULL destroy is safe. */
    audio_buffer_destroy(NULL);
}

/* 2. Empty buffer read returns zero samples. */
static void test_empty_read(void) {
    audio_buffer *buf = audio_buffer_create(64);
    float out[8];
    size_t got;

    if (buf == NULL) {
        fail("empty_read: create failed");
    }

    got = audio_buffer_read(buf, out, 8);
    expect_size("empty_read: got", 0, got);
    expect_size("empty_read: available_read", 0, audio_buffer_available_read(buf));
    audio_buffer_destroy(buf);
}

/* 3. Write then read returns samples in FIFO order. */
static void test_write_read_order(void) {
    audio_buffer *buf = audio_buffer_create(64);
    float in[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    size_t written, got;
    int i;

    if (buf == NULL) {
        fail("write_read_order: create failed");
    }

    written = audio_buffer_write(buf, in, 4);
    expect_size("write_read_order: written", 4, written);
    expect_size("write_read_order: available_read after write", 4, audio_buffer_available_read(buf));

    got = audio_buffer_read(buf, out, 4);
    expect_size("write_read_order: got", 4, got);

    for (i = 0; i < 4; i++) {
        expect_float("write_read_order: sample", in[i], out[i]);
    }

    expect_size("write_read_order: available_read after read", 0, audio_buffer_available_read(buf));
    audio_buffer_destroy(buf);
}

/* 4. Wraparound preserves order. */
static void test_wraparound(void) {
    /* Use capacity 8, fill it, drain half, fill again past the end. */
    audio_buffer *buf = audio_buffer_create(8);
    float in1[6] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    float drain[4];
    float in2[4] = { 7.0f, 8.0f, 9.0f, 10.0f };
    float out[6];
    size_t i;

    if (buf == NULL) {
        fail("wraparound: create failed");
    }

    audio_buffer_write(buf, in1, 6);
    audio_buffer_read(buf, drain, 4);    /* consume first 4 */
    audio_buffer_write(buf, in2, 4);     /* write 4 more, wraps internally */

    /* Buffer should now hold: 5,6,7,8,9,10 */
    size_t got = audio_buffer_read(buf, out, 6);
    expect_size("wraparound: got", 6, got);

    float expected[6] = { 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f };
    for (i = 0; i < 6; i++) {
        expect_float("wraparound: sample", expected[i], out[i]);
    }

    audio_buffer_destroy(buf);
}

/* 5. Read shortage returns available samples; caller responsible for silence. */
static void test_underrun_partial(void) {
    audio_buffer *buf = audio_buffer_create(64);
    float in[3] = { 0.1f, 0.2f, 0.3f };
    float out[8];
    size_t got;
    uint64_t underruns_before;

    if (buf == NULL) {
        fail("underrun_partial: create failed");
    }

    audio_buffer_write(buf, in, 3);
    underruns_before = audio_buffer_underrun_count(buf);

    /* Request 8 but only 3 are available. */
    got = audio_buffer_read(buf, out, 8);
    expect_size("underrun_partial: got", 3, got);
    expect_float("underrun_partial: sample0", 0.1f, out[0]);
    expect_float("underrun_partial: sample1", 0.2f, out[1]);
    expect_float("underrun_partial: sample2", 0.3f, out[2]);

    /* Underrun counter incremented once. */
    expect_u64("underrun_partial: underruns", underruns_before + 1, audio_buffer_underrun_count(buf));

    audio_buffer_destroy(buf);
}

/* 6. Overrun rejects excess new samples and increments overrun counter. */
static void test_overrun(void) {
    audio_buffer *buf = audio_buffer_create(4);
    float in[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    float out[4];
    size_t written;
    uint64_t overruns_before;
    size_t cap = audio_buffer_capacity(buf);

    if (buf == NULL) {
        fail("overrun: create failed");
    }

    /* Fill the buffer first. */
    written = audio_buffer_write(buf, in, cap);
    expect_size("overrun: first write", cap, written);

    overruns_before = audio_buffer_overrun_count(buf);

    /* Try to write more than capacity. */
    written = audio_buffer_write(buf, in, 2);
    expect_size("overrun: second write", 0, written);
    expect_u64("overrun: overrun count", overruns_before + 1, audio_buffer_overrun_count(buf));

    /* Existing samples are intact — first cap samples still readable. */
    size_t got = audio_buffer_read(buf, out, cap);
    expect_size("overrun: read after overrun", cap, got);
    expect_float("overrun: first sample", 1.0f, out[0]);

    audio_buffer_destroy(buf);
}

/* 7. Reset clears readable samples. */
static void test_reset(void) {
    audio_buffer *buf = audio_buffer_create(64);
    float in[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float out[4];
    size_t got;

    if (buf == NULL) {
        fail("reset: create failed");
    }

    audio_buffer_write(buf, in, 4);
    expect_size("reset: available before", 4, audio_buffer_available_read(buf));

    audio_buffer_reset(buf);

    expect_size("reset: available after", 0, audio_buffer_available_read(buf));

    got = audio_buffer_read(buf, out, 4);
    expect_size("reset: read after reset", 0, got);

    audio_buffer_destroy(buf);
}

/* 8. Capacity and available_read/write are consistent. */
static void test_capacity_consistency(void) {
    audio_buffer *buf = audio_buffer_create(16);
    size_t cap, avail_write, avail_read;
    float in[8];
    int i;

    if (buf == NULL) {
        fail("capacity_consistency: create failed");
    }

    cap = audio_buffer_capacity(buf);
    if (cap < 16) {
        fail("capacity_consistency: capacity too small");
    }

    avail_read  = audio_buffer_available_read(buf);
    avail_write = audio_buffer_available_write(buf);
    expect_size("capacity_consistency: read+write==cap (empty)",
        cap, avail_read + avail_write);

    for (i = 0; i < 8; i++) {
        in[i] = (float)i;
    }
    audio_buffer_write(buf, in, 8);

    avail_read  = audio_buffer_available_read(buf);
    avail_write = audio_buffer_available_write(buf);
    expect_size("capacity_consistency: read after write", 8, avail_read);
    expect_size("capacity_consistency: read+write==cap (half full)",
        cap, avail_read + avail_write);

    audio_buffer_destroy(buf);
}

int main(void) {
    test_create_destroy();
    test_empty_read();
    test_write_read_order();
    test_wraparound();
    test_underrun_partial();
    test_overrun();
    test_reset();
    test_capacity_consistency();

    printf("audio_buffer: all tests passed\n");
    return 0;
}

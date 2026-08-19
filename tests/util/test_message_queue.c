#include "message_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg)
{
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void test_push_pop(void)
{
    message_queue *q = message_queue_create(sizeof(int), 4);
    int values[] = { 10, 20, 30 };
    int out = 0;
    size_t i;

    if (q == NULL) {
        fail("create returned NULL");
    }

    for (i = 0; i < 3; i++) {
        if (!message_queue_push(q, &values[i])) {
            fail("push failed");
        }
    }

    for (i = 0; i < 3; i++) {
        if (!message_queue_try_pop(q, &out)) {
            fail("try_pop failed");
        }
        if (out != values[i]) {
            fail("value mismatch");
        }
    }

    if (message_queue_try_pop(q, &out)) {
        fail("expected empty queue");
    }

    message_queue_destroy(q);
}

static void test_full_rejects(void)
{
    message_queue *q = message_queue_create(sizeof(int), 2);
    int a = 1;
    int b = 2;
    int c = 3;

    if (q == NULL) {
        fail("create returned NULL");
    }

    if (!message_queue_push(q, &a) || !message_queue_push(q, &b)) {
        fail("fill failed");
    }
    if (message_queue_push(q, &c)) {
        fail("expected full queue to reject push");
    }

    message_queue_destroy(q);
}

int main(void)
{
    test_push_pop();
    test_full_rejects();
    printf("message_queue: all tests passed\n");
    return 0;
}

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct message_queue message_queue;

message_queue *message_queue_create(
    size_t item_size,
    size_t capacity);

void message_queue_destroy(message_queue *queue);

bool message_queue_push(
    message_queue *queue,
    const void *item);

bool message_queue_try_pop(
    message_queue *queue,
    void *out_item);

bool message_queue_wait_pop(
    message_queue *queue,
    void *out_item);

bool message_queue_wait_pop_timeout(
    message_queue *queue,
    void *out_item,
    uint32_t timeout_ms);

void message_queue_wake_all(message_queue *queue);

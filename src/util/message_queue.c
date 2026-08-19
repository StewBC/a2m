#include "message_queue.h"

#include "cond.h"
#include "mutex.h"

#include <stdlib.h>
#include <string.h>

struct message_queue {
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    bool woken;
    unsigned char *items;
    mutex *lock;
    cond *not_empty;
};

static void message_queue_copy_out(message_queue *queue, void *out_item) {
    memcpy(out_item, queue->items + (queue->head * queue->item_size), queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
}

message_queue *message_queue_create(
    size_t item_size,
    size_t capacity) {
    if (item_size == 0 || capacity == 0) {
        return NULL;
    }

    message_queue *queue = calloc(1, sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->item_size = item_size;
    queue->capacity = capacity;
    queue->items = malloc(item_size * capacity);
    queue->lock = mutex_create();
    queue->not_empty = cond_create();

    if (!queue->items || !queue->lock || !queue->not_empty) {
        message_queue_destroy(queue);
        return NULL;
    }

    return queue;
}

void message_queue_destroy(message_queue *queue) {
    if (!queue) {
        return;
    }

    cond_destroy(queue->not_empty);
    mutex_destroy(queue->lock);
    free(queue->items);
    free(queue);
}

bool message_queue_push(
    message_queue *queue,
    const void *item) {
    if (!queue || !item) {
        return false;
    }

    mutex_lock(queue->lock);
    if (queue->count == queue->capacity) {
        mutex_unlock(queue->lock);
        return false;
    }

    memcpy(queue->items + (queue->tail * queue->item_size), item, queue->item_size);
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    queue->woken = false;
    cond_signal(queue->not_empty);
    mutex_unlock(queue->lock);
    return true;
}

bool message_queue_try_pop(
    message_queue *queue,
    void *out_item) {
    if (!queue || !out_item) {
        return false;
    }

    mutex_lock(queue->lock);
    if (queue->count == 0) {
        mutex_unlock(queue->lock);
        return false;
    }

    message_queue_copy_out(queue, out_item);
    mutex_unlock(queue->lock);
    return true;
}

bool message_queue_wait_pop(
    message_queue *queue,
    void *out_item) {
    if (!queue || !out_item) {
        return false;
    }

    mutex_lock(queue->lock);
    while (queue->count == 0 && !queue->woken) {
        cond_wait(queue->not_empty, queue->lock);
    }

    if (queue->count == 0) {
        queue->woken = false;
        mutex_unlock(queue->lock);
        return false;
    }

    message_queue_copy_out(queue, out_item);
    mutex_unlock(queue->lock);
    return true;
}

bool message_queue_wait_pop_timeout(
    message_queue *queue,
    void *out_item,
    uint32_t timeout_ms) {
    if (!queue || !out_item) {
        return false;
    }

    mutex_lock(queue->lock);
    while (queue->count == 0 && !queue->woken) {
        if (!cond_wait_timeout(queue->not_empty, queue->lock, timeout_ms)) {
            break;
        }
    }

    if (queue->count == 0) {
        queue->woken = false;
        mutex_unlock(queue->lock);
        return false;
    }

    message_queue_copy_out(queue, out_item);
    mutex_unlock(queue->lock);
    return true;
}

void message_queue_wake_all(message_queue *queue) {
    if (!queue) {
        return;
    }

    mutex_lock(queue->lock);
    queue->woken = true;
    cond_broadcast(queue->not_empty);
    mutex_unlock(queue->lock);
}

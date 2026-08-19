#pragma once

typedef struct thread thread;

typedef int (*thread_entry_fn)(void *userdata);

thread *thread_create(
    const char *name,
    thread_entry_fn entry,
    void *userdata);

void thread_join(thread *t);
void thread_destroy(thread *t);

#include "thread.h"

#include <SDL.h>
#include <stdlib.h>

struct thread {
    SDL_Thread *handle;
};

thread *thread_create(
    const char *name,
    thread_entry_fn entry,
    void *userdata) {
    if (!entry) {
        return NULL;
    }

    thread *t = malloc(sizeof(*t));
    if (!t) {
        return NULL;
    }

    t->handle = SDL_CreateThread(entry, name, userdata);
    if (!t->handle) {
        free(t);
        return NULL;
    }

    return t;
}

void thread_join(thread *t) {
    if (!t || !t->handle) {
        return;
    }

    SDL_WaitThread(t->handle, NULL);
    t->handle = NULL;
}

void thread_destroy(thread *t) {
    if (!t) {
        return;
    }

    free(t);
}

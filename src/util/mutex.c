#include "mutex.h"

#include <SDL.h>
#include <stdlib.h>

struct mutex {
    SDL_mutex *handle;
};

mutex *mutex_create(void) {
    mutex *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }

    m->handle = SDL_CreateMutex();
    if (!m->handle) {
        free(m);
        return NULL;
    }

    return m;
}

void mutex_destroy(mutex *m) {
    if (!m) {
        return;
    }

    if (m->handle) {
        SDL_DestroyMutex(m->handle);
    }
    free(m);
}

void mutex_lock(mutex *m) {
    if (m && m->handle) {
        SDL_LockMutex(m->handle);
    }
}

void mutex_unlock(mutex *m) {
    if (m && m->handle) {
        SDL_UnlockMutex(m->handle);
    }
}

#include "cond.h"

#include "mutex.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdlib.h>

struct mutex {
    SDL_mutex *handle;
};

struct cond {
    SDL_cond *handle;
};

cond *cond_create(void) {
    cond *c = malloc(sizeof(*c));
    if (!c) {
        return NULL;
    }

    c->handle = SDL_CreateCond();
    if (!c->handle) {
        free(c);
        return NULL;
    }

    return c;
}

void cond_destroy(cond *c) {
    if (!c) {
        return;
    }

    if (c->handle) {
        SDL_DestroyCond(c->handle);
    }
    free(c);
}

void cond_wait(cond *c, mutex *m) {
    if (c && c->handle && m && m->handle) {
        SDL_CondWait(c->handle, m->handle);
    }
}

bool cond_wait_timeout(cond *c, mutex *m, uint32_t timeout_ms) {
    if (!c || !c->handle || !m || !m->handle) {
        return false;
    }

    return SDL_CondWaitTimeout(c->handle, m->handle, timeout_ms) == 0;
}

void cond_signal(cond *c) {
    if (c && c->handle) {
        SDL_CondSignal(c->handle);
    }
}

void cond_broadcast(cond *c) {
    if (c && c->handle) {
        SDL_CondBroadcast(c->handle);
    }
}

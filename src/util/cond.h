#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct cond cond;
typedef struct mutex mutex;

cond *cond_create(void);
void cond_destroy(cond *c);
void cond_wait(cond *c, mutex *m);
bool cond_wait_timeout(cond *c, mutex *m, uint32_t timeout_ms);
void cond_signal(cond *c);
void cond_broadcast(cond *c);

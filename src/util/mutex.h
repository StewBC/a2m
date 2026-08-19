#pragma once

typedef struct mutex mutex;

mutex *mutex_create(void);
void mutex_destroy(mutex *m);
void mutex_lock(mutex *m);
void mutex_unlock(mutex *m);

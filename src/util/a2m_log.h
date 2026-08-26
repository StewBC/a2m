#pragma once

#include "log.h"

#include <stdbool.h>

typedef enum a2m_log_level {
    A2M_LOG_LEVEL_ALL = 0,
    A2M_LOG_LEVEL_WARN,
    A2M_LOG_LEVEL_ERROR,
    A2M_LOG_LEVEL_NONE
} a2m_log_level;

/* Default policy is WARN. */
void a2m_log_init(void);
void a2m_log_apply(a2m_log_level level);

const char *a2m_log_level_name(a2m_log_level level);
bool a2m_log_level_from_string(const char *s, a2m_log_level *out_level);

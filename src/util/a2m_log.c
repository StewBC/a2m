#include "a2m_log.h"

#include <string.h>
#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

void a2m_log_init(void)
{
    a2m_log_apply(A2M_LOG_LEVEL_WARN);
}

void a2m_log_apply(a2m_log_level level)
{
    switch (level) {
    case A2M_LOG_LEVEL_ALL:
        log_set_quiet(false);
        log_set_level(LOG_TRACE);
        break;
    case A2M_LOG_LEVEL_ERROR:
        log_set_quiet(false);
        log_set_level(LOG_ERROR);
        break;
    case A2M_LOG_LEVEL_NONE:
        log_set_quiet(true);
        break;
    case A2M_LOG_LEVEL_WARN:
    default:
        log_set_quiet(false);
        log_set_level(LOG_WARN);
        break;
    }
}

const char *a2m_log_level_name(a2m_log_level level)
{
    switch (level) {
    case A2M_LOG_LEVEL_ALL:
        return "all";
    case A2M_LOG_LEVEL_ERROR:
        return "error";
    case A2M_LOG_LEVEL_NONE:
        return "none";
    case A2M_LOG_LEVEL_WARN:
    default:
        return "warn";
    }
}

bool a2m_log_level_from_string(const char *s, a2m_log_level *out_level)
{
    a2m_log_level level;

    if (s == NULL || out_level == NULL) {
        return false;
    }
    if (strcasecmp(s, "all") == 0) {
        level = A2M_LOG_LEVEL_ALL;
    } else if (strcasecmp(s, "warn") == 0) {
        level = A2M_LOG_LEVEL_WARN;
    } else if (strcasecmp(s, "error") == 0) {
        level = A2M_LOG_LEVEL_ERROR;
    } else if (strcasecmp(s, "none") == 0) {
        level = A2M_LOG_LEVEL_NONE;
    } else {
        return false;
    }
    *out_level = level;
    return true;
}

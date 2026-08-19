#include "control_deferred.h"

#include <string.h>

void control_deferred_clear(deferred_control_response *d)
{
    if (d == NULL) {
        return;
    }
    memset(d, 0, sizeof(*d));
}

deferred_control_response *control_deferred_reserve(
    deferred_control_table *table,
    const char **out_busy_msg)
{
    if (table == NULL) {
        if (out_busy_msg != NULL) {
            *out_busy_msg = "no-table";
        }
        return NULL;
    }
    if (table->entries[0].active) {
        if (out_busy_msg != NULL) {
            *out_busy_msg = "deferred-response-active";
        }
        return NULL;
    }
    control_deferred_clear(&table->entries[0]);
    return &table->entries[0];
}

deferred_control_response *control_deferred_active(deferred_control_table *table)
{
    if (table == NULL || !table->entries[0].active) {
        return NULL;
    }
    return &table->entries[0];
}

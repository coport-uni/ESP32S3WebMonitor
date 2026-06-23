#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One snapshot of the hotplate, mapped from the server's GET /status.
 * `connected` is the device's serial link state as the server reports it;
 * when false the numeric fields are not meaningful and the UI shows "--".
 */
typedef struct {
    bool  connected;
    float plate_c;
    float probe_c;
    bool  probe_valid;   /* false when the server returns probe = null */
    float speed_rpm;
    float target_c;
    float target_rpm;
    float safety_c;
    int   age_s;         /* age of the reading on the server, seconds */
} ui_status_t;

/**
 * Build the LVGL screen: a readings panel and a grid of touch control
 * buttons. Must be called inside bsp_display_lock / bsp_display_unlock.
 */
void ui_create(void);

/**
 * Push a fresh snapshot to the readings panel. Safe to call from any
 * task; takes the LVGL lock internally.
 */
void ui_set_status(const ui_status_t *st);

/**
 * Show an offline/placeholder state (WiFi down, server unreachable, not
 * configured). The numeric fields blank out and `reason` is written to
 * the status footer. Safe to call from any task.
 */
void ui_set_offline(const char *reason);

#ifdef __cplusplus
}
#endif

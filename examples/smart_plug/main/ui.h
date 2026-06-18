#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Action requested when a plug's on-screen button is pressed. */
typedef enum {
    UI_PLUG_ACTION_ON,
    UI_PLUG_ACTION_OFF,
} ui_plug_action_t;

/** A single plug's display state. `name` is borrowed for the duration of
 * the call only; the UI copies what it needs. */
typedef struct {
    const char *name;          /* plug name, e.g. "plug1" */
    bool        online;        /* server reached the plug on the last read */
    bool        is_on;         /* relay state (meaningful when online) */
    bool        has_power;     /* energy-meter reading available */
    bool        power_pending; /* online, power not available yet (settling) */
    float       power_w;       /* instantaneous power (meaningful when has_power) */
} ui_plug_t;

/**
 * Callback invoked on the LVGL task when a plug touch button is pressed.
 * It must NOT block (no HTTP, no long work) — it only forwards the request
 * to a worker, typically by posting to a queue.
 */
typedef void (*ui_plug_action_cb_t)(int plug_idx, ui_plug_action_t action);

/**
 * Build the full-screen plug-control UI: a header status line plus a
 * scrollable list of plug cards. Must be called inside
 * bsp_display_lock / bsp_display_unlock.
 */
void ui_create(void);

/**
 * Register the touch-button action sink. The stored callback is invoked
 * (on the LVGL task) whenever an ON / OFF / TOGGLE button is pressed.
 */
void ui_plugs_set_action_cb(ui_plug_action_cb_t cb);

/**
 * Replace the displayed plug list. When the count or any name differs
 * from the previous call the card list is rebuilt; otherwise the existing
 * cards are updated in place. Safe to call from any task; takes the LVGL
 * lock internally.
 */
void ui_plugs_replace(const ui_plug_t *plugs, int count);

/**
 * Update the header status line. `color_hex` uses 0xRRGGBB so callers do
 * not need to include lvgl.h.
 */
void ui_plugs_set_status(const char *msg, uint32_t color_hex);

#ifdef __cplusplus
}
#endif

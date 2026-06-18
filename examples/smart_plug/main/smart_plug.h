#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the smart-plug control + polling task.
 *
 * Spawns a background task that, once the network is up, discovers the
 * plugs exposed by the FastAPI server (CONFIG_SMART_PLUG_SERVER_URL) via
 * GET /plugs, then periodically reads each plug's on/off state and power
 * and pushes them into the UI via ui_plugs_replace().
 *
 * Also registers a UI action callback (ui_plugs_set_action_cb) so the
 * on-screen ON / OFF / TOGGLE touch buttons enqueue a control request that
 * the task performs off the LVGL thread — each POST contacts the physical
 * plug and takes ~1-3 s, which must never block the UI.
 *
 * @return ESP_OK once the task is created, regardless of whether the
 *         first discovery has happened yet.
 */
esp_err_t smart_plug_init(void);

#ifdef __cplusplus
}
#endif

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Control actions the UI can request. Setpoint deltas are applied to the
 * server's last-reported target by the client task, so the UI never has
 * to track absolute values.
 */
typedef enum {
    HP_CMD_TEMP_DELTA,    /* arg: change in degrees C   */
    HP_CMD_SPEED_DELTA,   /* arg: change in rpm         */
    HP_CMD_HEATER_START,
    HP_CMD_HEATER_STOP,
    HP_CMD_MOTOR_START,
    HP_CMD_MOTOR_STOP,
} hp_cmd_type_t;

typedef struct {
    hp_cmd_type_t type;
    float         arg;
} hp_command_t;

/**
 * @brief  Start the background client task.
 *
 * Creates the command queue and a task that polls `GET /status` every
 * CONFIG_HOTPLATE_POLL_INTERVAL_S seconds, pushes readings to the UI,
 * and drains queued control commands as `POST /control/...` requests.
 * All HTTP runs on this one task, so requests never overlap.
 *
 * @return ESP_OK on success, or an esp_err_t on allocation failure.
 */
esp_err_t hotplate_client_init(void);

/**
 * @brief  Queue a control command for the client task to send.
 *
 * Non-blocking and safe to call from the LVGL/UI task; the actual HTTP
 * request happens later on the client task.
 *
 * @param  cmd  Command to enqueue (copied).
 * @return true if queued, false if the queue is full or uninitialized.
 */
bool      hotplate_client_enqueue(const hp_command_t *cmd);

#ifdef __cplusplus
}
#endif

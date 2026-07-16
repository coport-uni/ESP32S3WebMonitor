#include <stdio.h>

#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "sdkconfig.h"

#include "ui.h"
#include "buttons_check.h"
#include "network.h"
#include "hotplate_client.h"

static const char *TAG = "main";

/* The two physical buttons mirror the temperature +/- touch buttons, so
 * the demo can drive the setpoint without the touchscreen. */
static void on_config_pressed(void)
{
    hp_command_t cmd = {
        .type = HP_CMD_TEMP_DELTA,
        .arg = (float)CONFIG_HOTPLATE_TEMP_STEP_C,
    };
    hotplate_client_enqueue(&cmd);
}

static void on_mute_pressed(void)
{
    hp_command_t cmd = {
        .type = HP_CMD_TEMP_DELTA,
        .arg = -(float)CONFIG_HOTPLATE_TEMP_STEP_C,
    };
    hotplate_client_enqueue(&cmd);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Hotplate monitor starting");

    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    buttons_callbacks_t btn_cbs = {
        .on_config = on_config_pressed,
        .on_mute   = on_mute_pressed,
    };
    buttons_check_init(&btn_cbs);

    network_init();
    ESP_ERROR_CHECK(hotplate_client_init());

    ESP_LOGI(TAG, "init complete");
}

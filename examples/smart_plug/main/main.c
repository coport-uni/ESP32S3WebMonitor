#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "ui.h"
#include "network.h"
#include "smart_plug.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Smart plug client starting");

    /* Init order mirrors the BOX-3 BSP contract: the shared I2C bus must
     * come up before the display (touch lives on it), the backlight is off
     * until explicitly turned on, and every lv_* call from app_main must be
     * wrapped in the BSP display lock. */
    ESP_ERROR_CHECK(bsp_i2c_init());
    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    network_init();
    smart_plug_init();

    ESP_LOGI(TAG, "init complete");
}

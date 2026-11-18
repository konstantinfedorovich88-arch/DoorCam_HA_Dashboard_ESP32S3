#include "esp_log.h"
#include "esp_err.h"
#include "dashboard_board.h"
#include "dashboard_ha.h"
#include "dashboard_ui.h"

static const char *TAG = "DASH_APP";

void app_main(void)
{
    ESP_LOGI(TAG, "DoorCam HA Dashboard ESP32-S3 initial scaffold");
    /* The LVGL UI must only start after the exact RGB LCD/touch board port
       creates the display, initializes LVGL, and arranges a handler task. */
    esp_err_t err = dashboard_board_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display integration pending: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "RGB color-bar diagnostic running; LVGL and touch not enabled yet");
    /* Do not call dashboard_ui_create() until LVGL is initialized. */
    (void)dashboard_ha_start;
    (void)dashboard_ui_create;
}

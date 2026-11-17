#include "dashboard_ha.h"
#include "esp_log.h"

static const char *TAG = "DASH_HA";

esp_err_t dashboard_ha_start(void)
{
    ESP_LOGW(TAG, "HA transport is not configured yet; no credentials stored");
    return ESP_ERR_NOT_SUPPORTED;
}

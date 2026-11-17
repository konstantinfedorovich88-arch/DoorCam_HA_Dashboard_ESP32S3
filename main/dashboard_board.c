#include "dashboard_board.h"
#include "esp_log.h"

static const char *TAG = "DASH_BOARD";

esp_err_t dashboard_board_init(void)
{
    ESP_LOGW(TAG, "Waveshare LCD/GT911 board port is not yet integrated");
    return ESP_ERR_NOT_SUPPORTED;
}

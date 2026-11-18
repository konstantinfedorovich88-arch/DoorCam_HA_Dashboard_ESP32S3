#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

/* RGB panel is initialized before LVGL; touch will be added separately. */
esp_err_t dashboard_board_init(void);
esp_lcd_panel_handle_t dashboard_board_panel(void);

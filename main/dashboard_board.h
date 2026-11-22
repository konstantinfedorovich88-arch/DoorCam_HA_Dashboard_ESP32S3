#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"

/* RGB panel is initialized before LVGL; touch will be added separately. */
esp_err_t dashboard_board_init(void);
esp_lcd_panel_handle_t dashboard_board_panel(void);
esp_err_t dashboard_board_backlight_set(bool enabled);

esp_err_t dashboard_board_touch_init(void);
bool dashboard_board_touch_read(uint16_t *x, uint16_t *y);

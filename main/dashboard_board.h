#pragma once
#include "esp_err.h"

/* Requires hardware-specific RGB LCD, GT911 and CH422G integration. */
esp_err_t dashboard_board_init(void);

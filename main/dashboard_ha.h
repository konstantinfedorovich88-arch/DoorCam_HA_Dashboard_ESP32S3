#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Network probing is performed on a worker task, never the LVGL task. */
esp_err_t dashboard_ha_start(void);
esp_err_t dashboard_ha_probe(const char *host, unsigned port);
void dashboard_ha_status(char *out, unsigned capacity);

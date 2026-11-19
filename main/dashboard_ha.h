#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Network probing is performed on a worker task, never the LVGL task. */
esp_err_t dashboard_ha_start(void);
esp_err_t dashboard_ha_probe(const char *host, unsigned port);
void dashboard_ha_status(char *out, unsigned capacity);
esp_err_t dashboard_ha_save_token(const char *token);
esp_err_t dashboard_ha_auth_test(void);
bool dashboard_ha_has_token(void);
esp_err_t dashboard_ha_web_start(void);
void dashboard_ha_setup_code(char *out, unsigned capacity);

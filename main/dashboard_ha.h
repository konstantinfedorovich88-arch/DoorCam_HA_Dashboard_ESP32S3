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
#define DASH_HA_MAX_ENTITIES 8
typedef struct {
    char entity_id[80];
    char state[64];
    char title[64];
} dashboard_ha_entity_t;
typedef struct {
    bool connected;
    unsigned count;
    bool panel_loaded;
    unsigned panel_count;
    dashboard_ha_entity_t panel_cards[DASH_HA_MAX_ENTITIES];
    dashboard_ha_entity_t entities[DASH_HA_MAX_ENTITIES];
} dashboard_ha_snapshot_t;
esp_err_t dashboard_ha_live_start(void);
void dashboard_ha_live_snapshot(dashboard_ha_snapshot_t *out);

bool dashboard_ha_person_detected(void);

bool dashboard_ha_is_connected(void);

/* Configured dashboard sensors, independent from the HA Lovelace panel. */
#define DASH_HA_WATCH_MAX 8
void dashboard_ha_watch_set(const char ids[DASH_HA_WATCH_MAX][80], unsigned count);
bool dashboard_ha_watch_get(const char *entity_id, char *state, unsigned capacity);

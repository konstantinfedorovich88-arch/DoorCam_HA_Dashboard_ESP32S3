#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define DASH_WIFI_MAX_AP 12
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} dash_wifi_ap_t;
typedef struct {
    bool scanning;
    bool connected;
    bool connecting;
    unsigned generation;
    unsigned count;
    char status[96];
    char ip[16];
    dash_wifi_ap_t aps[DASH_WIFI_MAX_AP];
} dash_wifi_snapshot_t;
esp_err_t dash_wifi_init(void);
esp_err_t dash_wifi_scan(void);
esp_err_t dash_wifi_connect(const char *ssid, const char *password);
void dash_wifi_snapshot(dash_wifi_snapshot_t *out);

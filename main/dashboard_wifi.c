#include "dashboard_wifi.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "DASH_WIFI";
static SemaphoreHandle_t s_lock;
static dash_wifi_snapshot_t s_state;
static bool s_ready;
static bool s_want_connect;
static bool s_scan_wait_disconnect;
static bool s_resume_after_scan;

static esp_err_t start_scan_now(void)
{
    wifi_scan_config_t cfg = {.show_hidden = false};
    return esp_wifi_scan_start(&cfg, false);
}


static void set_status(const char *value)
{
    snprintf(s_state.status, sizeof(s_state.status), "%s", value);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        wifi_ap_record_t records[DASH_WIFI_MAX_AP] = {0};
        uint16_t count = DASH_WIFI_MAX_AP;
        esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.scanning = false;
        s_state.count = err == ESP_OK ? count : 0;
        for (unsigned i = 0; i < s_state.count; ++i) {
            snprintf(s_state.aps[i].ssid, sizeof(s_state.aps[i].ssid), "%s",
                     (const char *)records[i].ssid);
            s_state.aps[i].rssi = records[i].rssi;
            s_state.aps[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
        }
        s_state.generation++;
        if (!s_state.connected && !s_state.connecting)
            set_status(err == ESP_OK ? "Scan complete" : "Wi-Fi scan failed");
        xSemaphoreGive(s_lock);
        if (s_resume_after_scan) {
            s_resume_after_scan = false;
            if (s_want_connect) esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_want_connect) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "STA disconnected: reason=%u, scan=%d, reconnect=%d",
                 disc ? (unsigned)disc->reason : 0U,
                 (int)s_state.scanning, (int)s_want_connect);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.connected = false;
        s_state.connecting = s_want_connect;
        s_state.ip[0] = 0;
        set_status(s_want_connect ? "Reconnecting..." : "Disconnected");
        xSemaphoreGive(s_lock);
        if (s_scan_wait_disconnect) {
            s_scan_wait_disconnect = false;
            esp_err_t scan_err = start_scan_now();
            if (scan_err != ESP_OK) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_state.scanning = false;
                set_status("Scan failed");
                xSemaphoreGive(s_lock);
                s_resume_after_scan = false;
                if (s_want_connect) esp_wifi_connect();
            }
        } else if (!s_state.scanning && s_want_connect) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.connected = true;
        s_state.connecting = false;
        snprintf(s_state.ip, sizeof(s_state.ip), IPSTR, IP2STR(&evt->ip_info.ip));
        set_status("Connected");
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&evt->ip_info.ip));
    }
}

esp_err_t dash_wifi_init(void)
{
    if (s_ready) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    set_status("Not connected");
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_FLASH)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    wifi_config_t saved = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK && saved.sta.ssid[0]) {
        s_want_connect = true;
        s_state.connecting = true;
        set_status("Connecting to saved Wi-Fi...");
    }
    if ((err = esp_wifi_start()) != ESP_OK) return err;
    s_ready = true;
    return ESP_OK;
}

esp_err_t dash_wifi_scan(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state.scanning) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    bool was_connecting = s_state.connecting;
    s_state.scanning = true;
    set_status("Scanning...");
    xSemaphoreGive(s_lock);

    /* A scan cannot start while STA is connecting. Pause reconnect,
       disconnect first and launch the scan from the DISCONNECTED event. */
    if (was_connecting) {
        s_resume_after_scan = s_want_connect;
        s_scan_wait_disconnect = true;
        esp_err_t err = esp_wifi_disconnect();
        if (err == ESP_OK) return ESP_OK;
        s_scan_wait_disconnect = false;
        s_resume_after_scan = false;
    }
    esp_err_t err = start_scan_now();
    if (err != ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_state.scanning = false;
        set_status("Scan start failed");
        xSemaphoreGive(s_lock);
    }
    return err;
}

esp_err_t dash_wifi_connect(const char *ssid, const char *password)
{
    if (!s_ready || !ssid || !password) return ESP_ERR_INVALID_ARG;
    size_t slen = strlen(ssid), plen = strlen(password);
    if (!slen || slen > 32 || plen > 63 || (plen > 0 && plen < 8)) return ESP_ERR_INVALID_ARG;
    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, slen);
    memcpy(cfg.sta.password, password, plen);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    s_want_connect = false;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.connected = false;
    s_state.connecting = true;
    s_state.ip[0] = 0;
    set_status("Connecting...");
    xSemaphoreGive(s_lock);
    s_want_connect = true;
    return esp_wifi_connect();
}

void dash_wifi_snapshot(dash_wifi_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s_lock) {
        snprintf(out->status, sizeof(out->status), "Wi-Fi not initialized");
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

#include "dashboard_ha.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include "nvs.h"
#include "esp_http_client.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "DASH_HA";
static SemaphoreHandle_t s_lock;
static char s_status[100] = "Not tested";
static char s_host[64];
static unsigned s_port;
static bool s_busy;
#define HA_TOKEN_MAX 512
#define HA_NVS_NS "doorcam_ha"
#define HA_NVS_KEY "token"

static void set_status(const char *message)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_status, sizeof(s_status), "%s", message);
    xSemaphoreGive(s_lock);
}

void dashboard_ha_status(char *out, unsigned capacity)
{
    if (!out || !capacity) return;
    if (!s_lock) { snprintf(out, capacity, "Not initialized"); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out, capacity, "%s", s_status);
    xSemaphoreGive(s_lock);
}

static void probe_task(void *arg)
{
    (void)arg;
    char host[sizeof(s_host)];
    unsigned port;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(host, sizeof(host), "%s", s_host);
    port = s_port;
    xSemaphoreGive(s_lock);

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    int lookup = getaddrinfo(host, port_str, &hints, &addresses);
    bool reachable = false;
    if (lookup == 0 && addresses) {
        for (struct addrinfo *a = addresses; a; a = a->ai_next) {
            int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) continue;
            struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) reachable = true;
            close(fd);
            if (reachable) break;
        }
        freeaddrinfo(addresses);
    }
    set_status(reachable ? "HA port reachable (TCP)" : "HA unreachable: check LAN/Tailscale route");
    ESP_LOGI(TAG, "HA TCP probe %s:%u: %s", host, port, reachable ? "reachable" : "unreachable");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_busy = false;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t dashboard_ha_start(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t dashboard_ha_probe(const char *host, unsigned port)
{
    if (!host || !host[0] || strlen(host) >= sizeof(s_host) || !port || port > 65535)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = dashboard_ha_start();
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_busy) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    s_busy = true;
    snprintf(s_host, sizeof(s_host), "%s", host);
    s_port = port;
    snprintf(s_status, sizeof(s_status), "Checking HA connection...");
    xSemaphoreGive(s_lock);
    if (xTaskCreate(probe_task, "ha_probe", 4096, NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_busy = false;
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Token is stored in NVS, not source control. Enable NVS encryption for
 * protection against physical flash extraction before production deployment. */
bool dashboard_ha_has_token(void)
{
    nvs_handle_t handle;
    if (nvs_open(HA_NVS_NS, NVS_READONLY, &handle) != ESP_OK) return false;
    size_t size = 0;
    esp_err_t err = nvs_get_str(handle, HA_NVS_KEY, NULL, &size);
    nvs_close(handle);
    return err == ESP_OK && size > 1 && size <= HA_TOKEN_MAX;
}

esp_err_t dashboard_ha_save_token(const char *token)
{
    if (!token) return ESP_ERR_INVALID_ARG;
    size_t len = strlen(token);
    if (len < 20 || len >= HA_TOKEN_MAX) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(HA_NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, HA_NVS_KEY, token);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) set_status("Token saved; tap Test auth");
    return err;
}

static void auth_task(void *arg)
{
    (void)arg;
    char *token = calloc(HA_TOKEN_MAX, 1);
    if (!token) { set_status("Out of memory"); goto done; }
    nvs_handle_t handle;
    if (nvs_open(HA_NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        set_status("No token saved"); free(token); goto done;
    }
    size_t len = HA_TOKEN_MAX;
    esp_err_t err = nvs_get_str(handle, HA_NVS_KEY, token, &len);
    nvs_close(handle);
    if (err != ESP_OK) { set_status("Cannot read token"); free(token); goto done; }

    esp_http_client_config_t cfg = {
        .url = "http://192.168.0.215:8123/api/",
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { set_status("HTTP client initialization failed"); free(token); goto done; }
    char *header = malloc(strlen(token) + 8);
    if (!header) {
        set_status("Out of memory");
    } else {
        snprintf(header, strlen(token) + 8, "Bearer %s", token);
        esp_http_client_set_header(client, "Authorization", header);
        esp_http_client_set_header(client, "Accept", "application/json");
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int code = esp_http_client_get_status_code(client);
            if (code == 200) set_status("HA authenticated (HTTP 200)");
            else if (code == 401 || code == 403) set_status("HA rejected token (401/403)");
            else {
                char status[80];
                snprintf(status, sizeof(status), "HA HTTP response: %d", code);
                set_status(status);
            }
            ESP_LOGI(TAG, "HA authentication HTTP status: %d", code);
        } else {
            set_status("HA auth request failed; check Wi-Fi");
            ESP_LOGW(TAG, "HA auth request: %s", esp_err_to_name(err));
        }
        memset(header, 0, strlen(token) + 8);
        free(header);
    }
    esp_http_client_cleanup(client);
    memset(token, 0, HA_TOKEN_MAX);
    free(token);
done:
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_busy = false;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t dashboard_ha_auth_test(void)
{
    esp_err_t err = dashboard_ha_start();
    if (err != ESP_OK) return err;
    if (!dashboard_ha_has_token()) return ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_busy) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    s_busy = true;
    snprintf(s_status, sizeof(s_status), "Checking HA token...");
    xSemaphoreGive(s_lock);
    if (xTaskCreate(auth_task, "ha_auth", 6144, NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_busy = false;
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

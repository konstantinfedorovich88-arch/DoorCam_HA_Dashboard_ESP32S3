#include "dashboard_ha.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
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

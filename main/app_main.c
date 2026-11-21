#include "esp_log.h"
#include "esp_sntp.h"
#include <stdlib.h>
#include <time.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "dashboard_board.h"
#include "ui.h"
#include "dashboard_wifi.h"
#include "dashboard_ha.h"

static const char *TAG = "DASH_APP";
#define DISPLAY_W 800
#define DISPLAY_H 480
#define DRAW_LINES 16

static uint32_t lv_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void lcd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px)
{
    esp_lcd_panel_handle_t panel = dashboard_board_panel();
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                              area->x2 + 1, area->y2 + 1, px);
    if (err != ESP_OK) ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(err));
    lv_display_flush_ready(display);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static uint16_t last_x, last_y;
    uint16_t x = last_x, y = last_y;
    bool pressed = dashboard_board_touch_read(&x, &y);
    static bool previous_pressed = false;
    if (pressed) { last_x = x; last_y = y; }
    if (pressed != previous_pressed) {
        ESP_LOGI(TAG, "LVGL touch %s at x=%u y=%u",
                 pressed ? "PRESSED" : "RELEASED", last_x, last_y);
        previous_pressed = pressed;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void dashboard_lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "DoorCam HA Dashboard LVGL display test");
    esp_err_t err = dashboard_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(err));
        return;
    }

    lv_init();
    lv_tick_set_cb(lv_tick_ms);
    lv_display_t *display = lv_display_create(DISPLAY_W, DISPLAY_H);
    if (!display) {
        ESP_LOGE(TAG, "LVGL display creation failed");
        return;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    const size_t bytes = DISPLAY_W * DRAW_LINES * 2;
    /* Keep LVGL rendering out of PSRAM to avoid starving RGB DMA during Wi-Fi scans. */
    void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer allocation failed");
        return;
    }
    lv_display_set_buffers(display, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, lcd_flush);
    if (dashboard_board_touch_init() == ESP_OK) {
        lv_indev_t *touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, touch_read_cb);
        lv_indev_set_display(touch, display);
        /* Poll touch every 15 ms instead of LVGL's default interval.
           This reduces input latency without changing RGB DMA or Wi-Fi timing. */
        lv_timer_t *touch_timer = lv_indev_get_read_timer(touch);
        if (touch_timer) lv_timer_set_period(touch_timer, 15);
        ESP_LOGI(TAG, "GT911 LVGL touch input registered (15 ms polling)");
    } else {
        ESP_LOGW(TAG, "Touch unavailable; UI will remain display-only");
    }
    ui_init();
    ESP_LOGI(TAG, "LVGL UI created; Wi-Fi settings available");

    while (true) {
        ui_tick();
        uint32_t wait_ms = lv_timer_handler();
        /* Keep touch and keyboard latency low without busy-spinning LVGL. */
        if (wait_ms < 3) wait_ms = 3;
        if (wait_ms > 10) wait_ms = 10;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

void app_main(void)
{
    esp_err_t wifi_err = dash_wifi_init();
    if (wifi_err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(wifi_err));
    setenv("TZ", "MSK-3", 1);
    tzset();
    if (wifi_err == ESP_OK) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_init();
        esp_err_t web_err = dashboard_ha_web_start();
        if (web_err != ESP_OK) ESP_LOGE(TAG, "HA web setup failed: %s", esp_err_to_name(web_err));
    }
    BaseType_t ok = xTaskCreatePinnedToCore(dashboard_lvgl_task, "dashboard_lvgl",
                                            16384, NULL, 7, NULL, 1);
    if (ok != pdPASS) ESP_LOGE(TAG, "Failed to create LVGL task");
}

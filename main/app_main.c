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
extern void dashboard_screen_touch_activity(void);
#include "ui.h"
#include "eez_ui/fonts.h"
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
    if (pressed) { last_x = x; last_y = y; dashboard_screen_touch_activity(); }
    if (pressed != previous_pressed) {
        ESP_LOGI(TAG, "LVGL touch %s at x=%u y=%u",
                 pressed ? "PRESSED" : "RELEASED", last_x, last_y);
        previous_pressed = pressed;
    }
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void dashboard_start_network(void);

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
    /* Extend the LVGL object heap with PSRAM; keep RGB draw buffers internal. */
    const size_t pool_bytes = 512 * 1024;
    void *pool = heap_caps_malloc(pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pool) {
        if (lv_mem_add_pool(pool, pool_bytes)) {
            ESP_LOGI(TAG, "LVGL PSRAM pool added: %u bytes", (unsigned)pool_bytes);
        } else {
            heap_caps_free(pool);
            ESP_LOGE(TAG, "LVGL PSRAM pool registration failed");
        }
    } else {
        ESP_LOGE(TAG, "LVGL PSRAM pool allocation failed");
    }
    lv_mem_monitor_t mem;
    lv_mem_monitor(&mem);
    ESP_LOGI(TAG, "LVGL heap: free=%u largest=%u", (unsigned)mem.free_size, (unsigned)mem.free_biggest_size);
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
        if (touch_timer) lv_timer_set_period(touch_timer, 10);
        ESP_LOGI(TAG, "GT911 LVGL touch input registered (10 ms polling)");
    } else {
        ESP_LOGW(TAG, "Touch unavailable; UI will remain display-only");
    }
    lv_obj_t *splash = lv_screen_active();
    lv_obj_set_style_bg_color(splash, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    lv_obj_t *welcome = lv_label_create(splash);
    lv_label_set_text(welcome, "Вас приветствует");
    lv_obj_set_style_text_font(welcome, &ui_font_roboto, 0);
    lv_obj_set_style_text_color(welcome, lv_color_hex(0xffffff), 0);
    /* Double the rendered Cyrillic font without changing the shared UI font. */
    lv_obj_set_style_transform_scale(welcome, 512, 0);
    /* Scale around the label centre, not its top-left corner. */
    lv_obj_set_style_transform_pivot_x(welcome, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(welcome, lv_pct(50), 0);
    lv_obj_align(welcome, LV_ALIGN_CENTER, 0, -100);
    lv_obj_t *home = lv_label_create(splash);
    lv_label_set_text(home, "умный дом");
    lv_obj_set_style_text_font(home, &ui_font_roboto, 0);
    lv_obj_set_style_text_color(home, lv_color_hex(0x4ade80), 0);
    /* Double the rendered Cyrillic font without changing the shared UI font. */
    lv_obj_set_style_transform_scale(home, 512, 0);
    /* Scale around the label centre, not its top-left corner. */
    lv_obj_set_style_transform_pivot_x(home, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(home, lv_pct(50), 0);
    lv_obj_align(home, LV_ALIGN_CENTER, 0, 15);
    lv_obj_t *loading = lv_label_create(splash);
    lv_label_set_text(loading, "Загрузка системы...");
    lv_obj_set_style_text_font(loading, &ui_font_roboto, 0);
    lv_obj_set_style_text_color(loading, lv_color_hex(0xb9cbe0), 0);
    /* Double the rendered Cyrillic font without changing the shared UI font. */
    lv_obj_set_style_transform_scale(loading, 512, 0);
    /* Scale around the label centre, not its top-left corner. */
    lv_obj_set_style_transform_pivot_x(loading, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(loading, lv_pct(50), 0);
    lv_obj_align(loading, LV_ALIGN_CENTER, 0, 120);
    uint32_t splash_start = lv_tick_get();
    ESP_LOGI(TAG, "RGB diagnostic: 10-second splash begins (Wi-Fi OFF)");
    bool three_seconds_logged = false;
    while (lv_tick_elaps(splash_start) < 10000) {
        if (!three_seconds_logged && lv_tick_elaps(splash_start) >= 3000) {
            three_seconds_logged = true;
            ESP_LOGI(TAG, "RGB diagnostic: splash at 3 seconds, Wi-Fi still OFF");
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "RGB diagnostic: splash at 10 seconds, Wi-Fi still OFF; starting network next");
    dashboard_start_network();
    ui_init();
    if (lv_screen_active() != splash) lv_obj_delete(splash);
    ESP_LOGI(TAG, "LVGL UI created; Wi-Fi settings available");

    /* UI data/status polling does not need to run at the display handler rate.
     * Keep LVGL input/timers responsive while reducing repeated HA mutex and
     * Wi-Fi snapshot reads during continuous RGB DMA scanout. */
    uint32_t last_ui_poll = lv_tick_get() - 100;
    while (true) {
        uint32_t now_ms = lv_tick_get();
        if (lv_tick_elaps(last_ui_poll) >= 100) {
            last_ui_poll = now_ms;
            ui_tick();
        }
        uint32_t wait_ms = lv_timer_handler();
        /* Preserve responsive touch/keyboard and LVGL animation timers. */
        if (wait_ms < 3) wait_ms = 3;
        if (wait_ms > 5) wait_ms = 5;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

static void dashboard_ha_start_task(void *arg)
{
    (void)arg;
    bool warned = false;
    for (;;) {
        dash_wifi_snapshot_t wifi;
        dash_wifi_snapshot(&wifi);
        if (wifi.connected) {
            if (dashboard_ha_has_token()) {
                esp_err_t err = dashboard_ha_live_start();
                ESP_LOGI(TAG, "HA WebSocket start: %s", esp_err_to_name(err));
                if (err == ESP_OK) break;
                /* Avoid repeatedly allocating resources after a failed start. */
                break;
            } else if (!warned) {
                ESP_LOGW(TAG, "HA token missing; configure via http://%s/", wifi.ip);
                warned = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

static void dashboard_start_network(void)
{
    ESP_LOGI(TAG, "RGB diagnostic: starting Wi-Fi after splash");
    esp_err_t wifi_err = dash_wifi_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(wifi_err));
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    esp_err_t web_err = dashboard_ha_web_start();
    if (web_err != ESP_OK) ESP_LOGE(TAG, "HA web setup failed: %s", esp_err_to_name(web_err));
    if (xTaskCreate(dashboard_ha_start_task, "ha_start", 4096, NULL, 4, NULL) != pdPASS)
        ESP_LOGE(TAG, "Failed to create HA startup task");
}

void app_main(void)
{
    setenv("TZ", "MSK-3", 1);
    tzset();
    ESP_LOGI(TAG, "RGB diagnostic: display and splash first; network starts afterwards");
    BaseType_t ok = xTaskCreatePinnedToCore(dashboard_lvgl_task, "dashboard_lvgl",
                                            16384, NULL, 7, NULL, 1);
    if (ok != pdPASS) ESP_LOGE(TAG, "Failed to create LVGL task");
}

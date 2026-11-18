#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "dashboard_board.h"
#include "dashboard_ui.h"

static const char *TAG = "DASH_APP";
#define DISPLAY_W 800
#define DISPLAY_H 480
#define DRAW_LINES 20

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
    if (pressed) { last_x = x; last_y = y; }
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
    void *buf1 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
        ESP_LOGI(TAG, "GT911 LVGL touch input registered");
    } else {
        ESP_LOGW(TAG, "Touch unavailable; UI will remain display-only");
    }
    dashboard_ui_create();
    ESP_LOGI(TAG, "LVGL UI created; Wi-Fi setup pending");

    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms < 5) wait_ms = 5;
        if (wait_ms > 30) wait_ms = 30;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

void app_main(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(dashboard_lvgl_task, "dashboard_lvgl",
                                            16384, NULL, 5, NULL, 1);
    if (ok != pdPASS) ESP_LOGE(TAG, "Failed to create LVGL task");
}

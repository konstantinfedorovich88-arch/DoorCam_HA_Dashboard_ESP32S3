#include "dashboard_board.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DASH_BOARD";
static esp_lcd_panel_handle_t s_panel;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_backlight_dev;
static uint8_t s_expander_output = 0x06;

/* Waveshare ESP32-S3-Touch-LCD-7 (not 7B). Verify PCB revision against
 * Waveshare schematic before relying on this provisional pin map. */
#define LCD_W 800
#define LCD_H 480

static esp_err_t enable_backlight(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 8,
        .scl_io_num = 9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "I2C bus");
    /* CH422G: 0x24 mode register, 0x38 output latch.
       EXIO1 touch reset; EXIO2 backlight enable (provisional). */
    i2c_device_config_t sys_cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = 0x24, .scl_speed_hz = 100000};
    i2c_device_config_t out_cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = 0x38, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t sys, out;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &sys_cfg, &sys), TAG, "CH422G mode");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &out_cfg, &out), TAG, "CH422G output");
    uint8_t mode = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(sys, &mode, 1, 100), TAG, "CH422G configure");
    s_backlight_dev = out;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(out, &s_expander_output, 1, 100), TAG, "backlight");
    return ESP_OK;
}

esp_err_t dashboard_board_init(void)
{
    ESP_LOGI(TAG, "Initializing provisional Waveshare 7-inch RGB panel");
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 12000000,
            .h_res = LCD_W, .v_res = LCD_H,
            .hsync_pulse_width = 4, .hsync_back_porch = 8, .hsync_front_porch = 8,
            .vsync_pulse_width = 4, .vsync_back_porch = 16, .vsync_front_porch = 16,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .num_fbs = 1,
        /* Reduce RGB DMA pressure on PSRAM during Wi-Fi and UI redraws. */
        .bounce_buffer_size_px = LCD_W * 32,
        .hsync_gpio_num = 46, .vsync_gpio_num = 3,
        .de_gpio_num = 5, .pclk_gpio_num = 7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45,
                          48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = true,
    };
    ESP_LOGI(TAG, "RGB diagnostic: PCLK=12MHz, bounce=%u lines, PSRAM framebuffer", 32U);
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "RGB panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "RGB init");
    void *framebuffer = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &framebuffer),
                        TAG, "RGB framebuffer");
    uint16_t *pixels = (uint16_t *)framebuffer;
    for (int y = 0; y < LCD_H; ++y) {
        uint16_t color = 0x10c4; /* RGB565 of dashboard background #101827 */
        for (int x = 0; x < LCD_W; ++x) pixels[y * LCD_W + x] = color;
    }
    esp_err_t err = enable_backlight();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight control failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "RGB startup background ready");
    return ESP_OK;
}

esp_err_t dashboard_board_backlight_set(bool enabled)
{
    if (!s_backlight_dev) return ESP_ERR_INVALID_STATE;
    uint8_t output = enabled ? (s_expander_output | 0x04) : (s_expander_output & (uint8_t)~0x04);
    esp_err_t err = i2c_master_transmit(s_backlight_dev, &output, 1, 100);
    if (err == ESP_OK) s_expander_output = output;
    return err;
}

esp_lcd_panel_handle_t dashboard_board_panel(void)
{
    return s_panel;
}

/* GT911: 16-bit register address, high byte first. Polling avoids an
 * unverified interrupt GPIO and leaves the proven RGB wiring untouched. */
static i2c_master_dev_handle_t s_touch;

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(s_touch, addr, sizeof(addr), data, len, 50);
}

static esp_err_t gt911_write(uint16_t reg, uint8_t value)
{
    uint8_t data[] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return i2c_master_transmit(s_touch, data, sizeof(data), 50);
}

esp_err_t dashboard_board_touch_init(void)
{
    const uint8_t addresses[] = {0x5d, 0x14};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i],
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t dev = NULL;
        esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev);
        if (err != ESP_OK) continue;
        s_touch = dev;
        uint8_t id[4] = {0};
        err = gt911_read(0x8140, id, sizeof(id));
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "GT911 detected at 0x%02x, product ID: %c%c%c%c",
                     addresses[i], id[0], id[1], id[2], id[3]);
            return ESP_OK;
        }
        s_touch = NULL;
        i2c_master_bus_rm_device(dev);
    }
    ESP_LOGW(TAG, "GT911 not detected at 0x5d or 0x14");
    return ESP_ERR_NOT_FOUND;
}

bool dashboard_board_touch_read(uint16_t *x, uint16_t *y)
{
    if (!s_touch) return false;
    uint8_t status = 0;
    if (gt911_read(0x814e, &status, 1) != ESP_OK) return false;
    if (!(status & 0x80)) return false;
    bool pressed = false;
    if ((status & 0x0f) > 0 && (status & 0x0f) <= 5) {
        uint8_t point[8] = {0};
        if (gt911_read(0x814f, point, sizeof(point)) == ESP_OK) {
            uint16_t px = (uint16_t)point[1] | ((uint16_t)point[2] << 8);
            uint16_t py = (uint16_t)point[3] | ((uint16_t)point[4] << 8);
            /* GT911 first point begins at 0x814F: track ID, X_L, X_H, Y_L, Y_H. */
            uint16_t screen_x = px;
            uint16_t screen_y = py;
            if (screen_x < LCD_W && screen_y < LCD_H) {
                *x = screen_x;
                *y = screen_y;
                pressed = true;
            }
        }
    }
    (void)gt911_write(0x814e, 0);
    return pressed;
}

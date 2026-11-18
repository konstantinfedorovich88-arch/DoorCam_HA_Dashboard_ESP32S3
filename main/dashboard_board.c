#include "dashboard_board.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DASH_BOARD";
static esp_lcd_panel_handle_t s_panel;

/* Waveshare ESP32-S3-Touch-LCD-7 (not 7B). Verify PCB revision against
 * Waveshare schematic before relying on this provisional pin map. */
#define LCD_W 800
#define LCD_H 480

static esp_err_t enable_backlight(void)
{
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 8,
        .scl_io_num = 9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &bus), TAG, "I2C bus");
    /* CH422G: 0x24 mode register, 0x38 output latch.
       EXIO1 touch reset; EXIO2 backlight enable (provisional). */
    i2c_device_config_t sys_cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = 0x24, .scl_speed_hz = 100000};
    i2c_device_config_t out_cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                   .device_address = 0x38, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t sys, out;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &sys_cfg, &sys), TAG, "CH422G mode");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &out_cfg, &out), TAG, "CH422G output");
    uint8_t mode = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(sys, &mode, 1, 100), TAG, "CH422G configure");
    uint8_t output = 0x06;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(out, &output, 1, 100), TAG, "backlight");
    return ESP_OK;
}

esp_err_t dashboard_board_init(void)
{
    ESP_LOGI(TAG, "Initializing provisional Waveshare 7-inch RGB panel");
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = LCD_W, .v_res = LCD_H,
            .hsync_pulse_width = 4, .hsync_back_porch = 8, .hsync_front_porch = 8,
            .vsync_pulse_width = 4, .vsync_back_porch = 16, .vsync_front_porch = 16,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 1,
        .bounce_buffer_size_px = LCD_W * 10,
        .psram_trans_align = 64,
        .hsync_gpio_num = 46, .vsync_gpio_num = 3,
        .de_gpio_num = 5, .pclk_gpio_num = 7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45,
                          48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG, "RGB panel create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "RGB reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "RGB init");
    void *framebuffer = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &framebuffer),
                        TAG, "RGB framebuffer");
    uint16_t *pixels = (uint16_t *)framebuffer;
    for (int y = 0; y < LCD_H; ++y) {
        uint16_t color = y < 160 ? 0xF800 : (y < 320 ? 0x07E0 : 0x001F);
        for (int x = 0; x < LCD_W; ++x) pixels[y * LCD_W + x] = color;
    }
    esp_err_t err = enable_backlight();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight control failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "RGB diagnostic pattern ready: red / green / blue");
    return ESP_OK;
}

#include <string.h>
#include <time.h>
#include <stdio.h>

#include "screens.h"
#include "../dashboard_ha.h"
#include "../dashboard_wifi.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

#if LV_FONT_MONTSERRAT_40
#define HOME_VALUE_FONT (&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_28
#define HOME_VALUE_FONT (&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
#define HOME_VALUE_FONT (&lv_font_montserrat_24)
#else
#define HOME_VALUE_FONT (&ui_font_roboto)
#endif

#if LV_FONT_MONTSERRAT_48
#define HOME_CLOCK_FONT (&lv_font_montserrat_48)
#elif LV_FONT_MONTSERRAT_22
#define HOME_CLOCK_FONT (&lv_font_montserrat_22)
#else
#define HOME_CLOCK_FONT HOME_VALUE_FONT
#endif
static lv_obj_t *home_clock_time;
static lv_obj_t *home_clock_date;
static lv_obj_t *home_camera_button;
static lv_obj_t *home_ha_status;
static lv_obj_t *home_wifi_status;
static lv_obj_t *settings_main;
static lv_obj_t *settings_wifi_page;
static lv_obj_t *wifi_info_label;
static lv_obj_t *wifi_networks_label;
static uint32_t wifi_page_last_tick;
static unsigned wifi_list_generation = (unsigned)-1;
static lv_obj_t *wifi_list;
static lv_obj_t *wifi_password_page;
static lv_obj_t *wifi_password_input;
static lv_obj_t *wifi_password_message;
static lv_obj_t *wifi_keyboard;
static char wifi_selected_ssid[33];
static bool wifi_selected_secure;
static bool wifi_connection_pending;
static uint32_t wifi_connection_start;

static void wifi_password_close(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(wifi_password_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(wifi_password_input, "");
    wifi_connection_pending = false;
}

static void wifi_connect_submit(lv_event_t *e)
{
    (void)e;
    const char *password = wifi_selected_secure ? lv_textarea_get_text(wifi_password_input) : "";
    size_t length = strlen(password);
    if (wifi_selected_secure && (length < 8 || length > 63)) {
        lv_label_set_text(wifi_password_message, "Пароль: от 8 до 63 символов");
        return;
    }
    esp_err_t err = dash_wifi_connect(wifi_selected_ssid, password);
    lv_textarea_set_text(wifi_password_input, "");
    if (err != ESP_OK) {
        lv_label_set_text(wifi_password_message, "Ошибка запуска подключения");
        return;
    }
    lv_label_set_text(wifi_password_message, "Подключение...");
    wifi_connection_pending = true;
    wifi_connection_start = lv_tick_get();
}

static void wifi_network_selected(lv_event_t *e)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    dash_wifi_snapshot_t wifi;
    dash_wifi_snapshot(&wifi);
    if (index >= wifi.count || wifi.scanning) return;
    snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s", wifi.aps[index].ssid);
    wifi_selected_secure = wifi.aps[index].secure;
    lv_obj_add_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_password_page, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(wifi_password_input, "");
    lv_label_set_text(wifi_password_message, wifi_selected_secure ? "Введите пароль сети" : "Открытая сеть");
    lv_obj_add_flag(wifi_password_input, wifi_selected_secure ? LV_OBJ_FLAG_CLICKABLE : LV_OBJ_FLAG_HIDDEN);
    if (wifi_selected_secure) lv_obj_remove_flag(wifi_password_input, LV_OBJ_FLAG_HIDDEN);
    wifi_connection_pending = false;
}

static void wifi_password_refresh(void)
{
    if (!wifi_password_page || lv_obj_has_flag(wifi_password_page, LV_OBJ_FLAG_HIDDEN) ||
        !wifi_connection_pending) return;
    dash_wifi_snapshot_t wifi;
    dash_wifi_snapshot(&wifi);
    if (wifi.connected) {
        lv_label_set_text(wifi_password_message, "Подключено! Нажмите Назад");
        wifi_connection_pending = false;
    } else if (!wifi.connecting && lv_tick_elaps(wifi_connection_start) > 3000) {
        lv_label_set_text(wifi_password_message, "Нет соединения. Проверьте пароль");
        wifi_connection_pending = false;
    } else if (lv_tick_elaps(wifi_connection_start) > 20000) {
        lv_label_set_text(wifi_password_message, "Ожидание сети. Проверьте пароль");
        wifi_connection_pending = false;
    }
}


static void settings_wifi_back(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_main, LV_OBJ_FLAG_HIDDEN);
}

static void settings_wifi_open(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(settings_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN);
    wifi_page_last_tick = 0;
    wifi_list_generation = (unsigned)-1;
}

static void settings_wifi_scan(lv_event_t *event)
{
    (void)event;
    esp_err_t err = dash_wifi_scan();
    if (err == ESP_OK) lv_label_set_text(wifi_networks_label, "Поиск сетей...");
    else lv_label_set_text(wifi_networks_label, "Не удалось начать поиск");
}

static void settings_wifi_refresh(void)
{
    if (!settings_wifi_page || lv_obj_has_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN)) return;
    if (wifi_page_last_tick && lv_tick_elaps(wifi_page_last_tick) < 1000) return;
    wifi_page_last_tick = lv_tick_get();
    dash_wifi_snapshot_t wifi;
    dash_wifi_snapshot(&wifi);
    char status[160];
    snprintf(status, sizeof(status), "Состояние: %s\nIP: %s",
             wifi.connected ? "Подключено" : (wifi.connecting ? "Подключение..." : "Нет связи"),
             wifi.ip[0] ? wifi.ip : "--");
    lv_label_set_text(wifi_info_label, status);
    if (wifi.scanning) return;
    if (wifi_list_generation == wifi.generation) return;
    wifi_list_generation = wifi.generation;
    lv_obj_clean(wifi_list);
    for (unsigned i = 0; i < wifi.count; i++) {
        lv_obj_t *button = lv_button_create(wifi_list);
        lv_obj_set_width(button, lv_pct(100));
        lv_obj_set_height(button, 43);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x24354b), 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_add_event_cb(button, wifi_network_selected, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *label = lv_label_create(button);
        char line[96];
        snprintf(line, sizeof(line), "%s  %d dBm %s",
                 wifi.aps[i].ssid, (int)wifi.aps[i].rssi,
                 wifi.aps[i].secure ? "*" : "");
        lv_label_set_text(label, line);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_center(label);
    }
    if (wifi.count == 0) {
        lv_obj_t *label = lv_label_create(wifi_list);
        lv_label_set_text(label, "Сети не найдены");
    }
}


static lv_obj_t *home_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
static lv_obj_t *home_label(lv_obj_t *parent, const char *value, int x, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_label_set_text(o, value);
    return o;
}

static lv_obj_t *home_pending_button(lv_obj_t *parent, int x, int y, const char *symbol, const char *title)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 372, 82);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x24354b), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x24354b), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_t *icon = lv_label_create(b);
    lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x94a9c2), 0);
    lv_label_set_text(icon, symbol);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *label = home_label(b, title, 0, 0, &ui_font_roboto, 0xb9cbe0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -14);
    /* Controls remain disabled until their actual services are connected. */
    lv_obj_add_state(b, LV_STATE_DISABLED);
    return b;
}

void create_screen_door_cam() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.door_cam = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(obj, &ui_font_roboto, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        objects.obj0 = home_card(parent_obj, 12, 8, 704, 52);
        objects.obj2 = home_label(objects.obj0, "Мой дом", 16, 15, &ui_font_roboto, 0xffffff);
        objects.obj1 = home_card(parent_obj, 12, 66, 382, 132);
        objects.obj3 = home_label(objects.obj1, "Погода сейчас", 16, 8, &ui_font_roboto, 0xb9cbe0);
        objects.obj4 = home_label(objects.obj1, "--", 16, 32, HOME_VALUE_FONT, 0xffffff);
        lv_obj_t *out = home_card(parent_obj, 406, 66, 382, 132);
        home_label(out, "На улице", 16, 8, &ui_font_roboto, 0xb9cbe0);
        home_label(out, "-- C", 16, 32, HOME_VALUE_FONT, 0xffffff);
        home_label(out, "Влажность: -- %", 16, 100, &ui_font_roboto, 0x7fc8f8);
        lv_obj_t *inside = home_card(parent_obj, 12, 202, 382, 132);
        home_label(inside, "Климат в доме", 16, 8, &ui_font_roboto, 0xb9cbe0);
        home_label(inside, "-- C", 16, 32, HOME_VALUE_FONT, 0xffffff);
        home_label(inside, "Влажность: -- %", 16, 100, &ui_font_roboto, 0x7fc8f8);
        lv_obj_t *clock = home_card(parent_obj, 406, 202, 382, 132);
        home_clock_time = home_label(clock, "--:--", 16, 32, HOME_CLOCK_FONT, 0x4ade80);
        home_clock_date = home_label(clock, "--.--.----", 16, 100, &ui_font_roboto, 0xb9cbe0);
        lv_obj_align(home_clock_time, LV_ALIGN_CENTER, 0, -12);
        lv_obj_set_style_transform_pivot_x(home_clock_time, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(home_clock_time, LV_PCT(50), 0);
        lv_obj_set_style_transform_scale_x(home_clock_time, 400, 0);
        lv_obj_set_style_transform_scale_y(home_clock_time, 650, 0);
        lv_obj_align(home_clock_date, LV_ALIGN_BOTTOM_MID, 0, -3);
        lv_obj_set_style_text_align(home_clock_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(home_clock_date, LV_TEXT_ALIGN_CENTER, 0);
        home_pending_button(parent_obj, 20, 348, LV_SYMBOL_POWER, "Мастер-выключатель");
        home_camera_button = home_pending_button(parent_obj, 408, 348, LV_SYMBOL_VIDEO, "Просмотр камеры");
        home_ha_status = home_label(parent_obj, "Home Assistant: --", 16, 447, &ui_font_roboto, 0xb9cbe0);
        home_wifi_status = home_label(parent_obj, "Wi-Fi: --", 408, 447, &ui_font_roboto, 0xb9cbe0);
        {
            // BtnSettings
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects.btn_settings = obj;
            lv_obj_set_pos(obj, 730, 10);
            lv_obj_set_size(obj, 50, 45);
            lv_obj_add_event_cb(obj, action_open_settings, LV_EVENT_CLICKED, (void *)0);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x344b66), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text_static(obj, "");
                }
            }
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 741, 19);
            lv_obj_set_size(obj, 28, 28);
            lv_image_set_src(obj, &img_icon_settings);
            lv_image_set_scale(obj, 250);
        }
        {
            // SettingsPopup — retain existing open/close callbacks.
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.settings_popup = obj;
            lv_obj_set_pos(obj, 170, 25);
            lv_obj_set_size(obj, 460, 430);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(obj, 0, 0);
            lv_obj_set_style_border_width(obj, 1, 0);
            lv_obj_set_style_border_color(obj, lv_color_hex(0x344b66), 0);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(obj, 18, 0);
            settings_main = obj;
            objects.obj6 = home_label(obj, "Настройки", 0, 0, &ui_font_roboto, 0xffffff);
            lv_obj_align(objects.obj6, LV_ALIGN_TOP_MID, 0, 22);
            objects.obj5 = lv_button_create(obj);
            lv_obj_set_pos(objects.obj5, 398, 10);
            lv_obj_set_size(objects.obj5, 46, 42);
            lv_obj_set_style_bg_color(objects.obj5, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(objects.obj5, action_close_settings, LV_EVENT_CLICKED, NULL);
            lv_obj_t *close = lv_label_create(objects.obj5);
            lv_label_set_text(close, "X");
            lv_obj_set_style_text_font(close, &ui_font_roboto, 0);
            lv_obj_set_style_text_color(close, lv_color_hex(0xf87171), 0);
            lv_obj_center(close);
            const char *titles[] = {"Wi-Fi", "Экран", "Звук", "Home Assistant", "Система"};
            const char *symbols[] = {LV_SYMBOL_WIFI, LV_SYMBOL_IMAGE, LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_HOME, LV_SYMBOL_SETTINGS};
            for (int i = 0; i < 5; i++) {
                lv_obj_t *button = lv_button_create(obj);
                lv_obj_set_pos(button, 16, 64 + i * 70);
                lv_obj_set_size(button, 426, 58);
                lv_obj_set_style_radius(button, 12, 0);
                lv_obj_set_style_pad_all(button, 0, 0);
                lv_obj_set_style_bg_color(button, lv_color_hex(0x24354b), 0);
                lv_obj_t *icon = home_label(button, symbols[i], 16, 0, LV_FONT_DEFAULT, 0x3cffde);
                lv_obj_align(icon, LV_ALIGN_LEFT_MID, 18, 0);
                lv_obj_t *label = home_label(button, titles[i], 0, 0, &ui_font_roboto, 0x3cffde);
                lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
                lv_obj_t *arrow = home_label(button, LV_SYMBOL_RIGHT, 0, 0, LV_FONT_DEFAULT, 0x94a9c2);
                lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -18, 0);
                if (i == 0) lv_obj_add_event_cb(button, settings_wifi_open, LV_EVENT_CLICKED, NULL);
                switch (i) {
                    case 0: objects.obj11 = label; break;
                    case 1: objects.obj10 = button; break;
                    case 2: objects.obj9 = label; break;
                    case 3: objects.obj8 = label; break;
                    case 4: objects.obj7 = button; break;
                }
            }
            settings_wifi_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_wifi_page, 170, 25);
            lv_obj_set_size(settings_wifi_page, 460, 430);
            lv_obj_set_style_bg_color(settings_wifi_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_wifi_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_wifi_page, 0, 0);
            lv_obj_set_style_radius(settings_wifi_page, 18, 0);
            lv_obj_set_style_pad_all(settings_wifi_page, 12, 0);
            lv_obj_remove_flag(settings_wifi_page, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(settings_wifi_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *back = lv_button_create(settings_wifi_page);
            lv_obj_set_pos(back, 0, 0);
            lv_obj_set_size(back, 80, 40);
            lv_obj_set_style_bg_color(back, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(back, settings_wifi_back, LV_EVENT_CLICKED, NULL);
            lv_obj_t *back_label = lv_label_create(back);
            lv_label_set_text(back_label, "Назад");
            lv_obj_center(back_label);
            lv_obj_t *wifi_title = home_label(settings_wifi_page, "Wi-Fi", 0, 0, &ui_font_roboto, 0x3cffde);
            lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 0, 12);
            wifi_info_label = home_label(settings_wifi_page, "Состояние: --", 4, 55, &ui_font_roboto, 0xffffff);
            lv_obj_t *scan = lv_button_create(settings_wifi_page);
            lv_obj_set_pos(scan, 4, 113);
            lv_obj_set_size(scan, 425, 44);
            lv_obj_set_style_bg_color(scan, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(scan, settings_wifi_scan, LV_EVENT_CLICKED, NULL);
            lv_obj_t *scan_label = lv_label_create(scan);
            lv_label_set_text(scan_label, "Поиск сетей");
            lv_obj_set_style_text_color(scan_label, lv_color_hex(0x3cffde), 0);
            lv_obj_center(scan_label);
            wifi_networks_label = home_label(settings_wifi_page, "Нажмите «Поиск сетей»", 4, 169, &ui_font_roboto, 0xb9cbe0);
            lv_obj_set_width(wifi_networks_label, 425);
            lv_label_set_long_mode(wifi_networks_label, LV_LABEL_LONG_WRAP);
            lv_obj_add_flag(wifi_networks_label, LV_OBJ_FLAG_HIDDEN);
            wifi_list = lv_obj_create(settings_wifi_page);
            lv_obj_set_pos(wifi_list, 0, 166);
            lv_obj_set_size(wifi_list, 430, 227);
            lv_obj_set_style_bg_opa(wifi_list, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(wifi_list, 0, 0);
            lv_obj_set_style_pad_all(wifi_list, 4, 0);
            lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(wifi_list, 6, 0);
            lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
            wifi_password_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(wifi_password_page, 50, 12);
            lv_obj_set_size(wifi_password_page, 700, 456);
            lv_obj_set_style_bg_color(wifi_password_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(wifi_password_page, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(wifi_password_page, 16, 0);
            lv_obj_set_style_pad_all(wifi_password_page, 12, 0);
            lv_obj_remove_flag(wifi_password_page, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(wifi_password_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t *pw_back = lv_button_create(wifi_password_page);
            lv_obj_set_pos(pw_back, 0, 0);
            lv_obj_set_size(pw_back, 82, 42);
            lv_obj_add_event_cb(pw_back, wifi_password_close, LV_EVENT_CLICKED, NULL);
            lv_obj_t *pw_back_text = lv_label_create(pw_back);
            lv_label_set_text(pw_back_text, "Назад");
            lv_obj_center(pw_back_text);
            wifi_password_message = lv_label_create(wifi_password_page);
            lv_obj_set_pos(wifi_password_message, 110, 10);
            lv_obj_set_style_text_color(wifi_password_message, lv_color_hex(0x4ade80), 0);
            lv_label_set_text(wifi_password_message, "Введите пароль");
            wifi_password_input = lv_textarea_create(wifi_password_page);
            lv_obj_set_pos(wifi_password_input, 0, 52);
            lv_obj_set_size(wifi_password_input, 530, 46);
            lv_textarea_set_one_line(wifi_password_input, true);
            lv_textarea_set_password_mode(wifi_password_input, true);
            lv_textarea_set_max_length(wifi_password_input, 63);
            lv_textarea_set_placeholder_text(wifi_password_input, "Пароль Wi-Fi");
            lv_obj_t *connect = lv_button_create(wifi_password_page);
            lv_obj_set_pos(connect, 540, 52);
            lv_obj_set_size(connect, 132, 46);
            lv_obj_set_style_bg_color(connect, lv_color_hex(0x247a59), 0);
            lv_obj_add_event_cb(connect, wifi_connect_submit, LV_EVENT_CLICKED, NULL);
            lv_obj_t *connect_text = lv_label_create(connect);
            lv_label_set_text(connect_text, "Подключить");
            lv_obj_center(connect_text);
            wifi_keyboard = lv_keyboard_create(wifi_password_page);
            lv_obj_set_pos(wifi_keyboard, 0, 111);
            lv_obj_set_size(wifi_keyboard, 672, 308);
            lv_keyboard_set_textarea(wifi_keyboard, wifi_password_input);
            lv_keyboard_set_mode(wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

        }
    }

    tick_screen_door_cam();
}

void tick_screen_door_cam() {
    settings_wifi_refresh();
    wifi_password_refresh();
    static uint32_t last_status_tick;
    static int last_ha = -1, last_wifi = -1;
    if (home_ha_status && home_wifi_status &&
        (last_ha < 0 || lv_tick_elaps(last_status_tick) >= 1000)) {
        last_status_tick = lv_tick_get();
        bool ha = dashboard_ha_is_connected();
        dash_wifi_snapshot_t wifi;
        dash_wifi_snapshot(&wifi);
        if (last_ha != ha) {
            last_ha = ha;
            lv_label_set_text(home_ha_status, ha ? "Home Assistant: подключён" : "Home Assistant: нет связи");
            lv_obj_set_style_text_color(home_ha_status, lv_color_hex(ha ? 0x4ade80 : 0xf87171), 0);
        }
        if (last_wifi != wifi.connected) {
            last_wifi = wifi.connected;
            lv_label_set_text(home_wifi_status, wifi.connected ? "Wi-Fi: подключён" : "Wi-Fi: нет связи");
            lv_obj_set_style_text_color(home_wifi_status, lv_color_hex(wifi.connected ? 0x4ade80 : 0xf87171), 0);
        }
    }
    static bool last_detected = false;
    static lv_obj_t *last_button = NULL;
    if (home_camera_button) {
        bool detected = dashboard_ha_person_detected();
        if (detected != last_detected || last_button != home_camera_button) {
            last_detected = detected;
            last_button = home_camera_button;
            lv_color_t bg = lv_color_hex(detected ? 0xffd600 : 0x24354b);
            lv_obj_set_style_bg_color(home_camera_button, bg, 0);
            lv_obj_set_style_bg_color(home_camera_button, bg, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(home_camera_button, 0),
                lv_color_hex(detected ? 0x202020 : 0x94a9c2), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(home_camera_button, 1),
                lv_color_hex(detected ? 0x202020 : 0xb9cbe0), 0);
        }
    }
    static time_t last_minute = -1;
    time_t now = time(NULL);
    if (!home_clock_time || !home_clock_date || now < 1704067200) return;
    if (now / 60 == last_minute) return;
    struct tm local;
    if (!localtime_r(&now, &local)) return;
    last_minute = now / 60;
    static const char *months[] = {"января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    static const char *days[] = {"воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"};
    char value[16], date[96];
    snprintf(value, sizeof(value), "%02d:%02d", local.tm_hour, local.tm_min);
    snprintf(date, sizeof(date), "%d %s, %s", local.tm_mday, months[local.tm_mon], days[local.tm_wday]);
    lv_label_set_text(home_clock_time, value);
    if (strcmp(lv_label_get_text(home_clock_date), date))
        lv_label_set_text(home_clock_date, date);
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_door_cam,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
    { "Roboto", &ui_font_roboto },
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_door_cam();
}
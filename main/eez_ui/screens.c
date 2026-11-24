#include <string.h>
#include <time.h>
#include <stdio.h>

#include "screens.h"
#include "../dashboard_ha.h"
#include "../dashboard_wifi.h"
#include "../dashboard_board.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
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
#elif LV_FONT_MONTSERRAT_22
#define HOME_VALUE_FONT (&lv_font_montserrat_22)
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
static lv_obj_t *settings_main;
static lv_obj_t *settings_screen_page;
static lv_obj_t *settings_system_page;
static lv_obj_t *system_info_label;
static lv_obj_t *system_restart_confirm;
static uint32_t system_last_tick;

static void system_settings_open(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_main, true);
    lv_obj_set_hidden(settings_system_page, false);
    system_last_tick = 0;
}

static void system_settings_back(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_system_page, true);
    lv_obj_set_hidden(settings_main, false);
}

static void system_restart_ask(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(system_restart_confirm, false);
}

static void system_restart_cancel(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(system_restart_confirm, true);
}

static void system_restart_now(lv_event_t *e)
{
    (void)e;
    esp_restart();
}

static void system_settings_refresh(void)
{
    if (!settings_system_page || lv_obj_is_hidden(settings_system_page)) return;
    if (system_last_tick && lv_tick_elaps(system_last_tick) < 2000) return;
    system_last_tick = lv_tick_get();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    const esp_app_desc_t *app = esp_app_get_description();
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    char info[420];
    snprintf(info, sizeof(info),
             "Чип: ESP32-S3, ревизия %u\\n"
             "Прошивка: %.32s\\n"
             "ESP-IDF: %s\\n"
             "RAM свободно: %u КБ\\n"
             "PSRAM свободно: %u КБ\\n"
             "Время работы: %llu мин",
             (unsigned)chip.revision, app->version, esp_get_idf_version(),
             (unsigned)(internal_free / 1024), (unsigned)(psram_free / 1024),
             (unsigned long long)(esp_timer_get_time() / 60000000ULL));
    if (system_info_label && strcmp(lv_label_get_text(system_info_label), info))
        lv_label_set_text(system_info_label, info);
}
static lv_obj_t *settings_ha_page;
static lv_obj_t *ha_info_label;
static uint32_t ha_page_last_tick;

static void ha_settings_open(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_main, true);
    lv_obj_set_hidden(settings_ha_page, false);
    ha_page_last_tick = 0;
}

static void ha_settings_back(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_ha_page, true);
    lv_obj_set_hidden(settings_main, false);
}

static void ha_settings_test(lv_event_t *e)
{
    (void)e;
    esp_err_t err = dashboard_ha_auth_test();
    if (err != ESP_OK && ha_info_label)
        lv_label_set_text_fmt(ha_info_label, "Проверка не запущена: %s", esp_err_to_name(err));
    ha_page_last_tick = 0;
}

static void ha_settings_refresh(void)
{
    if (!settings_ha_page || lv_obj_is_hidden(settings_ha_page)) return;
    if (ha_page_last_tick && lv_tick_elaps(ha_page_last_tick) < 1000) return;
    ha_page_last_tick = lv_tick_get();
    char status[100];
    dashboard_ha_status(status, sizeof(status));
    char code[20];
    dashboard_ha_setup_code(code, sizeof(code));
    char info[360];
    snprintf(info, sizeof(info),
             "Сервер: 192.168.0.215:8123\\n"
             "Соединение: %s\\n"
             "Токен: %s\\n"
             "Код настройки: %s\\n"
             "Проверка: %s",
             dashboard_ha_is_connected() ? "подключено" : "нет связи",
             dashboard_ha_has_token() ? "сохранён" : "не задан",
             code, status);
    if (ha_info_label && strcmp(lv_label_get_text(ha_info_label), info))
        lv_label_set_text(ha_info_label, info);
}
static lv_obj_t *settings_sound_page;
static lv_obj_t *sound_volume_label;
static lv_obj_t *sound_mic_label;
static uint8_t sound_volume = 60;
static uint8_t sound_mic = 60;

static void sound_settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("doorcam_sound", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "volume", sound_volume);
        nvs_set_u8(h, "mic_gain", sound_mic);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void sound_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("doorcam_sound", NVS_READONLY, &h) == ESP_OK) {
        uint8_t v;
        if (nvs_get_u8(h, "volume", &v) == ESP_OK && v <= 100) sound_volume = v;
        if (nvs_get_u8(h, "mic_gain", &v) == ESP_OK && v <= 100) sound_mic = v;
        nvs_close(h);
    }
}

static void sound_volume_changed(lv_event_t *e)
{
    sound_volume = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    if (sound_volume_label) lv_label_set_text_fmt(sound_volume_label, "Громкость: %u%%", sound_volume);
    sound_settings_save();
}

static void sound_mic_changed(lv_event_t *e)
{
    sound_mic = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    if (sound_mic_label) lv_label_set_text_fmt(sound_mic_label, "Микрофон: %u%%", sound_mic);
    sound_settings_save();
}

static void sound_settings_open(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_main, true);
    lv_obj_set_hidden(settings_sound_page, false);
}

static void sound_settings_back(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_sound_page, true);
    lv_obj_set_hidden(settings_main, false);
}

static lv_obj_t *screen_timeout_label;
static uint32_t screen_last_activity;
static uint32_t screen_timeout_ms = 60000;
static bool screen_backlight_on = true;

void dashboard_screen_touch_activity(void)
{
    screen_last_activity = lv_tick_get();
    if (!screen_backlight_on) {
        if (dashboard_board_backlight_set(true) == ESP_OK)
            screen_backlight_on = true;
    }
}

static void screen_settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("doorcam_screen", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "timeout_ms", screen_timeout_ms);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void screen_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("doorcam_screen", NVS_READONLY, &h) == ESP_OK) {
        uint32_t value;
        if (nvs_get_u32(h, "timeout_ms", &value) == ESP_OK &&
            (value == 0 || value == 30000 || value == 60000 || value == 300000))
            screen_timeout_ms = value;
        nvs_close(h);
    }
    screen_last_activity = lv_tick_get();
}

static void screen_timeout_update_label(void)
{
    if (!screen_timeout_label) return;
    const char *value = screen_timeout_ms == 0 ? "Никогда" :
        screen_timeout_ms == 30000 ? "30 секунд" :
        screen_timeout_ms == 60000 ? "1 минута" : "5 минут";
    lv_label_set_text(screen_timeout_label, value);
}

static void screen_timeout_cycle(lv_event_t *e)
{
    (void)e;
    screen_timeout_ms = screen_timeout_ms == 0 ? 30000 :
        screen_timeout_ms == 30000 ? 60000 :
        screen_timeout_ms == 60000 ? 300000 : 0;
    screen_timeout_update_label();
    screen_settings_save();
    dashboard_screen_touch_activity();
}

static void screen_settings_open(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_main, true);
    lv_obj_set_hidden(settings_screen_page, false);
    dashboard_screen_touch_activity();
}

static void screen_settings_back(lv_event_t *e)
{
    (void)e;
    lv_obj_set_hidden(settings_screen_page, true);
    lv_obj_set_hidden(settings_main, false);
}

/* Keep the panel and LVGL running; only CH422G backlight enable changes.
 * A short hold prevents AI detection flicker from toggling the backlight. */
#define SCREEN_PERSON_HOLD_MS 5000U

static void screen_settings_tick(void)
{
    static bool person_was_detected;
    static uint32_t person_last_seen;
    bool person_detected = dashboard_ha_person_detected();
    uint32_t now = lv_tick_get();

    if (person_detected) {
        person_last_seen = now;
        if (!person_was_detected || !screen_backlight_on) {
            dashboard_screen_touch_activity();
        }
    } else if (person_was_detected) {
        /* Start the configured idle timeout after detection ends. */
        screen_last_activity = now;
    }
    person_was_detected = person_detected;

    bool person_hold = person_detected ||
        (person_last_seen != 0 && lv_tick_elaps(person_last_seen) < SCREEN_PERSON_HOLD_MS);
    if (person_hold || !screen_backlight_on || screen_timeout_ms == 0) return;
    if (lv_tick_elaps(screen_last_activity) >= screen_timeout_ms) {
        if (dashboard_board_backlight_set(false) == ESP_OK)
            screen_backlight_on = false;
    }
}


static lv_obj_t *home_card(lv_obj_t *parent, int x, int y, int w, int h);
static lv_obj_t *home_label(lv_obj_t *parent, const char *value, int x, int y,
                            const lv_font_t *font, uint32_t color);

#define HOME_DYNAMIC_MAX 8
static lv_obj_t *home_dynamic_area;
static lv_obj_t *home_dynamic_values[HOME_DYNAMIC_MAX][2];
static char home_dynamic_ids[DASH_HA_WATCH_MAX][80];
static char home_dynamic_entity_ids[HOME_DYNAMIC_MAX][2][80];
static char home_layout_last[2048];
static unsigned home_dynamic_count;
static uint32_t home_layout_tick;
static uint32_t home_values_tick;
static bool home_watch_ready;


static lv_obj_t *weather_icons[HOME_DYNAMIC_MAX];
static char weather_last[HOME_DYNAMIC_MAX][64];

static void weather_dot(lv_obj_t *p, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_set_scrollable(o,false);
}
static void weather_icon_update(lv_obj_t *p, const char *state)
{
    if (!p) return;
    lv_obj_clean(p);
    bool sun = !strcmp(state,"sunny") || !strcmp(state,"clear-night") || !strcmp(state,"partlycloudy");
    bool cloud = !strcmp(state,"cloudy") || !strcmp(state,"partlycloudy") ||
                 !strcmp(state,"fog") || !strcmp(state,"windy") || !strcmp(state,"windy-variant");
    bool rain = !strcmp(state,"rainy") || !strcmp(state,"pouring") ||
                !strcmp(state,"lightning-rainy") || !strcmp(state,"hail");
    bool snow = !strcmp(state,"snowy") || !strcmp(state,"snowy-rainy");
    bool lightning = !strcmp(state,"lightning") || !strcmp(state,"lightning-rainy");
    if (sun) {
        weather_dot(p,8,7,37,37,0xfbbf24);
        weather_dot(p,25,0,4,5,0xfbbf24);
        weather_dot(p,25,46,4,5,0xfbbf24);
        weather_dot(p,0,24,5,4,0xfbbf24);
        weather_dot(p,47,24,5,4,0xfbbf24);
    }
    if (cloud || rain || snow || lightning) {
        weather_dot(p,6,26,56,25,0xcbd5e1);
        weather_dot(p,15,16,31,31,0xcbd5e1);
        weather_dot(p,36,21,26,26,0xcbd5e1);
    }
    for (int i=0;i<3 && (rain || snow);++i) {
        if (snow) home_label(p,"*",13+i*19,49,&ui_font_roboto,0x93c5fd);
        else weather_dot(p,16+i*18,51,4,11,0x60a5fa);
    }
    if (lightning) home_label(p,"!",31,45,&ui_font_roboto,0xfbbf24);
    if (!sun && !cloud && !rain && !snow && !lightning)
        home_label(p,"?",25,20,&ui_font_roboto,0x94a3b8);
}

static void home_dynamic_rebuild(const char *json)
{
    if (!home_dynamic_area) return;
    lv_obj_clean(home_dynamic_area);
    memset(home_dynamic_values, 0, sizeof(home_dynamic_values));
    memset(weather_icons, 0, sizeof(weather_icons));
    memset(weather_last, 0, sizeof(weather_last));
    memset(home_dynamic_ids, 0, sizeof(home_dynamic_ids));
    memset(home_dynamic_entity_ids, 0, sizeof(home_dynamic_entity_ids));
    home_dynamic_count = 0;
    cJSON *root = cJSON_Parse(json);
    cJSON *cards = cJSON_GetObjectItemCaseSensitive(root, "cards");
    if (cJSON_IsArray(cards)) {
        int column = 0, y = 0;
        cJSON *card = NULL;
        cJSON_ArrayForEach(card, cards) {
            if (home_dynamic_count >= HOME_DYNAMIC_MAX) break;
            cJSON *title = cJSON_GetObjectItemCaseSensitive(card, "title");
            cJSON *entity = cJSON_GetObjectItemCaseSensitive(card, "entity_id");
            cJSON *secondary = cJSON_GetObjectItemCaseSensitive(card, "entity_id_2");
            cJSON *size = cJSON_GetObjectItemCaseSensitive(card, "size");
            if (!cJSON_IsString(title) || !cJSON_IsString(entity)) continue;
            bool has_secondary = cJSON_IsString(secondary) && secondary->valuestring[0];
            bool wide = y != 0 && cJSON_IsString(size) && !strcmp(size->valuestring, "wide");
            if (wide && column) { column = 0; y += 132; }
            int x = column ? 394 : 0;
            lv_obj_t *panel = home_card(home_dynamic_area, x, y, wide ? 776 : 382, 128);
            lv_obj_t *heading = home_label(panel, title->valuestring, 16, 8, &ui_font_roboto, 0xb9cbe0);
            lv_obj_set_width(heading, wide ? 744 : 350);
            lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
            unsigned index = home_dynamic_count++;
            bool is_weather = !strncmp(entity->valuestring,"weather.",8);
            if (is_weather) {
                weather_icons[index] = lv_obj_create(panel);
                lv_obj_set_pos(weather_icons[index],12,39);
                lv_obj_set_size(weather_icons[index],72,72);
                lv_obj_set_style_bg_opa(weather_icons[index],LV_OPA_TRANSP,0);
                lv_obj_set_style_border_width(weather_icons[index],0,0);
                lv_obj_set_style_pad_all(weather_icons[index],0,0);
                lv_obj_set_scrollable(weather_icons[index],false);
                weather_icon_update(weather_icons[index],"unknown");
            }
            const lv_font_t *font = has_secondary || is_weather ? &ui_font_roboto : HOME_VALUE_FONT;
            home_dynamic_values[index][0] = home_label(panel, "--", is_weather ? 94 : 16, has_secondary ? 40 : 44, font, 0xffffff);
            lv_label_set_long_mode(home_dynamic_values[index][0], LV_LABEL_LONG_DOT);
            lv_obj_set_width(home_dynamic_values[index][0], is_weather ? (wide ? 620 : 260) : (wide ? 720 : 340));
            snprintf(home_dynamic_entity_ids[index][0], 80, "%s", entity->valuestring);
            if (has_secondary) {
                home_dynamic_values[index][1] = home_label(panel, "--", 16, 80, &ui_font_roboto, 0x7fc8f8);
                lv_label_set_long_mode(home_dynamic_values[index][1], LV_LABEL_LONG_DOT);
                lv_obj_set_width(home_dynamic_values[index][1], wide ? 720 : 340);
                snprintf(home_dynamic_entity_ids[index][1], 80, "%s", secondary->valuestring);
            }
            if (y == 0 || wide || column) { column = 0; y += 132; }
            else column = 1;
        }
    }
    cJSON_Delete(root);
    unsigned watch_count = 0;
    for (unsigned i = 0; i < home_dynamic_count; ++i)
        for (unsigned j = 0; j < 2; ++j)
            if (home_dynamic_entity_ids[i][j][0] && watch_count < DASH_HA_WATCH_MAX)
                snprintf(home_dynamic_ids[watch_count++], 80, "%s", home_dynamic_entity_ids[i][j]);
    home_watch_ready = false;
    home_values_tick = 0;
}

extern bool dashboard_ha_weather_get(char *out, unsigned capacity);
static dashboard_ha_snapshot_t home_panel_snapshot;
static void home_dynamic_tick(void)
{
    if (!home_dynamic_area) return;
    if (!home_layout_tick || lv_tick_elaps(home_layout_tick) >= 2000) {
        home_layout_tick = lv_tick_get();
        char layout[2048] = {0};
        dashboard_ha_live_snapshot(&home_panel_snapshot);
        if (home_panel_snapshot.panel_loaded) {
            cJSON *root=cJSON_CreateObject();
            cJSON *cards=cJSON_AddArrayToObject(root,"cards");
            for (unsigned i=0;cards && i<home_panel_snapshot.panel_count;i++) {
                dashboard_ha_entity_t *e=&home_panel_snapshot.panel_cards[i];
                cJSON *card=cJSON_CreateObject();
                if (!card) break;
                cJSON_AddItemToArray(cards,card);
                cJSON_AddStringToObject(card,"title",e->title);
                cJSON_AddStringToObject(card,"entity_id",e->entity_id);
            }
            char *json=cJSON_PrintUnformatted(root);
            if (json) { snprintf(layout,sizeof(layout),"%s",json); cJSON_free(json); }
            cJSON_Delete(root);
        }
        nvs_handle_t nvs;
        if (!home_panel_snapshot.panel_loaded && nvs_open("doorcam_cards", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(layout);
            nvs_get_str(nvs, "layout", layout, &len);
            nvs_close(nvs);
        }
        if (strcmp(layout, home_layout_last)) {
            snprintf(home_layout_last, sizeof(home_layout_last), "%s", layout);
            if (layout[0]) home_dynamic_rebuild(layout);
            else lv_obj_set_hidden(home_dynamic_area, true);
        }
        if (layout[0]) {
            lv_obj_set_hidden(home_dynamic_area, false);
            if (dashboard_ha_is_connected() && !home_watch_ready) {
                unsigned watch_count = 0;
                for (unsigned i = 0; i < DASH_HA_WATCH_MAX; ++i)
                    if (home_dynamic_ids[i][0]) ++watch_count;
                dashboard_ha_watch_set((const char (*)[80])home_dynamic_ids, watch_count);
                home_watch_ready = true;
            }
            if (!dashboard_ha_is_connected()) home_watch_ready = false;
        }
    }
    if (home_values_tick && lv_tick_elaps(home_values_tick) < 500) return;
    home_values_tick = lv_tick_get();
    for (unsigned i = 0; i < home_dynamic_count; ++i) {
        for (unsigned j = 0; j < 2; ++j) {
            if (!home_dynamic_values[i][j]) continue;
            char value[64] = {0};
            const char *display = "--";
            if (home_dynamic_entity_ids[i][j][0] &&
                dashboard_ha_watch_get(home_dynamic_entity_ids[i][j], value, sizeof(value)))
                display = value;
            if (j == 0 && weather_icons[i] && strcmp(weather_last[i],display)) {
                snprintf(weather_last[i],sizeof(weather_last[i]),"%s",display);
                weather_icon_update(weather_icons[i],display);
            }
            char weather[128];
            if (j == 0 && !strcmp(home_dynamic_entity_ids[i][j],"weather.home_assistant") && dashboard_ha_weather_get(weather,sizeof(weather))) display=weather;
            /* Use the larger Latin font only for numeric values; keep Cyrillic in Roboto. */
            if (j == 0 && !weather_icons[i]) {
                bool numeric = display[0] && strspn(display, "0123456789+-. ,%°CF") == strlen(display);
                const lv_font_t *font = numeric ? HOME_VALUE_FONT : &ui_font_roboto;
                if (lv_obj_get_style_text_font(home_dynamic_values[i][j], 0) != font)
                    lv_obj_set_style_text_font(home_dynamic_values[i][j], font, 0);
            }
            if (strcmp(lv_label_get_text(home_dynamic_values[i][j]), display))
                lv_label_set_text(home_dynamic_values[i][j], display);
        }
    }
}

static lv_obj_t *home_clock_time;
static lv_obj_t *home_clock_date;
static lv_obj_t *home_camera_button;
static lv_obj_t *home_ha_status;
static lv_obj_t *home_wifi_status;
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
    lv_obj_set_hidden(wifi_password_page, true);
    lv_obj_set_hidden(settings_wifi_page, false);
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
    lv_obj_set_hidden(settings_wifi_page, true);
    lv_obj_set_hidden(wifi_password_page, false);
    lv_textarea_set_text(wifi_password_input, "");
    lv_label_set_text(wifi_password_message, wifi_selected_secure ? "Введите пароль сети" : "Открытая сеть");
    lv_obj_set_clickable(wifi_password_input, wifi_selected_secure);
    lv_obj_set_hidden(wifi_password_input, !wifi_selected_secure);
    wifi_connection_pending = false;
}

static void wifi_password_refresh(void)
{
    if (!wifi_password_page || lv_obj_is_hidden(wifi_password_page) ||
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
    lv_obj_set_hidden(settings_wifi_page, true);
    lv_obj_set_hidden(settings_main, false);
}

static void settings_wifi_open(lv_event_t *event)
{
    (void)event;
    lv_obj_set_hidden(settings_main, true);
    lv_obj_set_hidden(settings_wifi_page, false);
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
    if (!settings_wifi_page || lv_obj_is_hidden(settings_wifi_page)) return;
    if (wifi_page_last_tick && lv_tick_elaps(wifi_page_last_tick) < 1000) return;
    wifi_page_last_tick = lv_tick_get();
    dash_wifi_snapshot_t wifi;
    dash_wifi_snapshot(&wifi);
    char status[160];
    snprintf(status, sizeof(status), "Состояние: %s\nIP: %s",
             wifi.connected ? "Подключено" : (wifi.connecting ? "Подключение..." : "Нет связи"),
             wifi.ip[0] ? wifi.ip : "--");
    if (strcmp(lv_label_get_text(wifi_info_label), status) != 0)
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
    lv_obj_set_scrollable(o, false);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x1d3044), 0);
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
    screen_settings_load();
    lv_obj_t *obj = lv_obj_create(0);
    objects.door_cam = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_text_font(obj, &ui_font_roboto, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        objects.obj0 = home_card(parent_obj, 12, 4, 278, 52);
        objects.obj2 = home_label(objects.obj0, "Мой дом", 0, 10, &ui_font_roboto, 0x4ade80);
        lv_obj_set_width(objects.obj2, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.obj2, LV_TEXT_ALIGN_LEFT, 0);
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
        lv_obj_t *clock = home_card(parent_obj, 406, 66, 382, 128);
        lv_obj_t *date_panel = home_card(parent_obj, 488, 4, 228, 52);
        home_clock_time = home_label(clock, "--:--", 8, 2, HOME_CLOCK_FONT, 0x4ade80);
        home_clock_date = home_label(date_panel, "--.--.----", 5, 12, &ui_font_roboto, 0xb9cbe0);
        lv_obj_set_width(home_clock_date, 218);
        lv_label_set_long_mode(home_clock_date, LV_LABEL_LONG_DOT);
        home_dynamic_area = lv_obj_create(parent_obj);
        lv_obj_set_pos(home_dynamic_area, 12, 66);
        lv_obj_set_size(home_dynamic_area, 776, 268);
        lv_obj_set_style_pad_all(home_dynamic_area, 0, 0);
        lv_obj_set_style_border_width(home_dynamic_area, 0, 0);
        lv_obj_set_style_bg_opa(home_dynamic_area, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(home_dynamic_area, lv_color_hex(0x101827), 0);
        lv_obj_set_scroll_dir(home_dynamic_area, LV_DIR_VER);
        lv_obj_set_hidden(home_dynamic_area, true);
        lv_obj_align(home_clock_time, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_foreground(clock);
        lv_obj_align(home_clock_date, LV_ALIGN_CENTER, 0, 0);
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
            /* Native 32x32 ARGB8888 icon. Avoid LVGL SW ARGB transform:
             * scaled draws can fail after HA state processing / memory churn. */
            lv_obj_set_size(obj, 32, 32);
            lv_image_set_src(obj, &img_icon_settings);
        }
        {
            // SettingsPopup — retain existing open/close callbacks.
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.settings_popup = obj;
            lv_obj_set_pos(obj, 170, 25);
            lv_obj_set_size(obj, 460, 430);
            lv_obj_set_hidden(obj, true);
            lv_obj_set_scrollable(obj, false);
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
                if (i == 1) lv_obj_add_event_cb(button, screen_settings_open, LV_EVENT_CLICKED, NULL);
                if (i == 2) lv_obj_add_event_cb(button, sound_settings_open, LV_EVENT_CLICKED, NULL);
                if (i == 3) lv_obj_add_event_cb(button, ha_settings_open, LV_EVENT_CLICKED, NULL);
                if (i == 4) lv_obj_add_event_cb(button, system_settings_open, LV_EVENT_CLICKED, NULL);
                switch (i) {
                    case 0: objects.obj11 = label; break;
                    case 1: objects.obj10 = button; break;
                    case 2: objects.obj9 = label; break;
                    case 3: objects.obj8 = label; break;
                    case 4: objects.obj7 = button; break;
                }
            }
            settings_screen_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_screen_page, 170, 25);
            lv_obj_set_size(settings_screen_page, 460, 430);
            lv_obj_set_style_bg_color(settings_screen_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_screen_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_screen_page, 0, 0);
            lv_obj_set_style_radius(settings_screen_page, 18, 0);
            lv_obj_set_style_pad_all(settings_screen_page, 12, 0);
            lv_obj_set_scrollable(settings_screen_page, false);
            lv_obj_set_hidden(settings_screen_page, true);
            lv_obj_t *screen_back = lv_button_create(settings_screen_page);
            lv_obj_set_pos(screen_back, 0, 0);
            lv_obj_set_size(screen_back, 82, 42);
            lv_obj_set_style_bg_color(screen_back, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(screen_back, screen_settings_back, LV_EVENT_CLICKED, NULL);
            lv_obj_t *screen_back_text = lv_label_create(screen_back);
            lv_label_set_text(screen_back_text, "Назад");
            lv_obj_center(screen_back_text);
            lv_obj_t *screen_title = home_label(settings_screen_page, "Экран", 0, 0, &ui_font_roboto, 0x3cffde);
            lv_obj_align(screen_title, LV_ALIGN_TOP_MID, 0, 12);
            home_label(settings_screen_page, "Отключение подсветки", 12, 75, &ui_font_roboto, 0xffffff);
            lv_obj_t *timeout_btn = lv_button_create(settings_screen_page);
            lv_obj_set_pos(timeout_btn, 12, 114);
            lv_obj_set_size(timeout_btn, 410, 55);
            lv_obj_set_style_bg_color(timeout_btn, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(timeout_btn, screen_timeout_cycle, LV_EVENT_CLICKED, NULL);
            screen_timeout_label = lv_label_create(timeout_btn);
            lv_obj_set_style_text_color(screen_timeout_label, lv_color_hex(0x3cffde), 0);
            lv_obj_center(screen_timeout_label);
            screen_timeout_update_label();
            lv_obj_t *screen_note = home_label(settings_screen_page,
                "Касание включает подсветку снова.", 12, 202, &ui_font_roboto, 0xb9cbe0);
            lv_obj_set_width(screen_note, 410);
            lv_label_set_long_mode(screen_note, LV_LABEL_LONG_WRAP);
            home_label(settings_screen_page, "Яркость: аппаратная проверка PWM", 12, 275, &ui_font_roboto, 0x94a9c2);

            /* Audio preferences are persisted now; codec/I2S wiring will be
             * connected separately once the audio hardware is selected. */
            sound_settings_load();
            settings_sound_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_sound_page, 170, 25);
            lv_obj_set_size(settings_sound_page, 460, 430);
            lv_obj_set_style_bg_color(settings_sound_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_sound_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_sound_page, 0, 0);
            lv_obj_set_style_radius(settings_sound_page, 18, 0);
            lv_obj_set_style_pad_all(settings_sound_page, 12, 0);
            lv_obj_set_scrollable(settings_sound_page, false);
            lv_obj_set_hidden(settings_sound_page, true);
            lv_obj_t *sound_back = lv_button_create(settings_sound_page);
            lv_obj_set_pos(sound_back, 0, 0);
            lv_obj_set_size(sound_back, 82, 42);
            lv_obj_set_style_bg_color(sound_back, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(sound_back, sound_settings_back, LV_EVENT_CLICKED, NULL);
            lv_obj_t *sound_back_text = lv_label_create(sound_back);
            lv_label_set_text(sound_back_text, "Назад");
            lv_obj_center(sound_back_text);
            lv_obj_t *sound_title = home_label(settings_sound_page, "Звук", 0, 0, &ui_font_roboto, 0x3cffde);
            lv_obj_align(sound_title, LV_ALIGN_TOP_MID, 0, 12);
            sound_volume_label = home_label(settings_sound_page, "", 12, 75, &ui_font_roboto, 0xffffff);
            lv_label_set_text_fmt(sound_volume_label, "Громкость: %u%%", sound_volume);
            lv_obj_t *vol_slider = lv_slider_create(settings_sound_page);
            lv_obj_set_pos(vol_slider, 20, 122);
            lv_obj_set_width(vol_slider, 390);
            lv_slider_set_range(vol_slider, 0, 100);
            lv_slider_set_value(vol_slider, sound_volume, LV_ANIM_OFF);
            lv_obj_add_event_cb(vol_slider, sound_volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
            sound_mic_label = home_label(settings_sound_page, "", 12, 187, &ui_font_roboto, 0xffffff);
            lv_label_set_text_fmt(sound_mic_label, "Микрофон: %u%%", sound_mic);
            lv_obj_t *mic_slider = lv_slider_create(settings_sound_page);
            lv_obj_set_pos(mic_slider, 20, 234);
            lv_obj_set_width(mic_slider, 390);
            lv_slider_set_range(mic_slider, 0, 100);
            lv_slider_set_value(mic_slider, sound_mic, LV_ANIM_OFF);
            lv_obj_add_event_cb(mic_slider, sound_mic_changed, LV_EVENT_VALUE_CHANGED, NULL);
            lv_obj_t *sound_note = home_label(settings_sound_page,
                "Настройки сохранены. Аудиомодуль пока не подключён.",
                12, 300, &ui_font_roboto, 0x94a9c2);
            lv_obj_set_width(sound_note, 410);
            lv_label_set_long_mode(sound_note, LV_LABEL_LONG_WRAP);

            settings_ha_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_ha_page, 170, 25);
            lv_obj_set_size(settings_ha_page, 460, 430);
            lv_obj_set_style_bg_color(settings_ha_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_ha_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_ha_page, 0, 0);
            lv_obj_set_style_radius(settings_ha_page, 18, 0);
            lv_obj_set_style_pad_all(settings_ha_page, 12, 0);
            lv_obj_set_scrollable(settings_ha_page, false);
            lv_obj_set_hidden(settings_ha_page, true);
            lv_obj_t *ha_back = lv_button_create(settings_ha_page);
            lv_obj_set_pos(ha_back, 0, 0);
            lv_obj_set_size(ha_back, 82, 42);
            lv_obj_set_style_bg_color(ha_back, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(ha_back, ha_settings_back, LV_EVENT_CLICKED, NULL);
            lv_obj_t *ha_back_text = lv_label_create(ha_back);
            lv_label_set_text(ha_back_text, "Назад");
            lv_obj_center(ha_back_text);
            lv_obj_t *ha_title = home_label(settings_ha_page, "Home Assistant", 0, 0, &ui_font_roboto, 0x3cffde);
            lv_obj_align(ha_title, LV_ALIGN_TOP_MID, 0, 12);
            ha_info_label = home_label(settings_ha_page, "Загрузка...", 12, 68, &ui_font_roboto, 0xffffff);
            lv_obj_set_width(ha_info_label, 410);
            lv_label_set_long_mode(ha_info_label, LV_LABEL_LONG_WRAP);
            lv_obj_t *ha_test = lv_button_create(settings_ha_page);
            lv_obj_set_pos(ha_test, 12, 257);
            lv_obj_set_size(ha_test, 410, 46);
            lv_obj_set_style_bg_color(ha_test, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(ha_test, ha_settings_test, LV_EVENT_CLICKED, NULL);
            lv_obj_t *ha_test_text = lv_label_create(ha_test);
            lv_label_set_text(ha_test_text, "Проверить токен");
            lv_obj_center(ha_test_text);
            lv_obj_t *ha_note = home_label(settings_ha_page,
                "Токен: откройте IP панели в браузере и введите код выше.",
                12, 320, &ui_font_roboto, 0x94a9c2);
            lv_obj_set_width(ha_note, 410);
            lv_label_set_long_mode(ha_note, LV_LABEL_LONG_WRAP);

            settings_system_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_system_page, 170, 25);
            lv_obj_set_size(settings_system_page, 460, 430);
            lv_obj_set_style_bg_color(settings_system_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_system_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_system_page, 0, 0);
            lv_obj_set_style_radius(settings_system_page, 18, 0);
            lv_obj_set_style_pad_all(settings_system_page, 12, 0);
            lv_obj_set_scrollable(settings_system_page, false);
            lv_obj_set_hidden(settings_system_page, true);
            lv_obj_t *system_back = lv_button_create(settings_system_page);
            lv_obj_set_pos(system_back, 0, 0);
            lv_obj_set_size(system_back, 82, 42);
            lv_obj_set_style_bg_color(system_back, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(system_back, system_settings_back, LV_EVENT_CLICKED, NULL);
            lv_obj_t *system_back_text = lv_label_create(system_back);
            lv_label_set_text(system_back_text, "Назад");
            lv_obj_center(system_back_text);
            lv_obj_t *system_title = home_label(settings_system_page, "Система", 0, 0, &ui_font_roboto, 0x3cffde);
            lv_obj_align(system_title, LV_ALIGN_TOP_MID, 0, 12);
            system_info_label = home_label(settings_system_page, "Загрузка...", 12, 72, &ui_font_roboto, 0xffffff);
            lv_obj_set_width(system_info_label, 410);
            lv_label_set_long_mode(system_info_label, LV_LABEL_LONG_WRAP);
            lv_obj_t *system_restart = lv_button_create(settings_system_page);
            lv_obj_set_pos(system_restart, 12, 300);
            lv_obj_set_size(system_restart, 410, 48);
            lv_obj_set_style_bg_color(system_restart, lv_color_hex(0x24354b), 0);
            lv_obj_add_event_cb(system_restart, system_restart_ask, LV_EVENT_CLICKED, NULL);
            lv_obj_t *system_restart_text = lv_label_create(system_restart);
            lv_label_set_text(system_restart_text, "Перезагрузить устройство");
            lv_obj_center(system_restart_text);
            system_restart_confirm = lv_obj_create(settings_system_page);
            lv_obj_set_pos(system_restart_confirm, 12, 110);
            lv_obj_set_size(system_restart_confirm, 410, 190);
            lv_obj_set_style_bg_color(system_restart_confirm, lv_color_hex(0x24354b), 0);
            lv_obj_set_scrollable(system_restart_confirm, false);
            lv_obj_set_hidden(system_restart_confirm, true);
            lv_obj_t *restart_prompt = home_label(system_restart_confirm, "Перезагрузить ESP32-S3?", 10, 10, &ui_font_roboto, 0xffffff);
            lv_obj_set_width(restart_prompt, 360);
            lv_obj_t *restart_cancel = lv_button_create(system_restart_confirm);
            lv_obj_set_pos(restart_cancel, 6, 90);
            lv_obj_set_size(restart_cancel, 170, 48);
            lv_obj_add_event_cb(restart_cancel, system_restart_cancel, LV_EVENT_CLICKED, NULL);
            lv_obj_t *cancel_text = lv_label_create(restart_cancel);
            lv_label_set_text(cancel_text, "Отмена");
            lv_obj_center(cancel_text);
            lv_obj_t *restart_confirm = lv_button_create(system_restart_confirm);
            lv_obj_set_pos(restart_confirm, 188, 90);
            lv_obj_set_size(restart_confirm, 170, 48);
            lv_obj_add_event_cb(restart_confirm, system_restart_now, LV_EVENT_CLICKED, NULL);
            lv_obj_t *confirm_text = lv_label_create(restart_confirm);
            lv_label_set_text(confirm_text, "Перезагрузить");
            lv_obj_center(confirm_text);
            settings_wifi_page = lv_obj_create(parent_obj);
            lv_obj_set_pos(settings_wifi_page, 170, 25);
            lv_obj_set_size(settings_wifi_page, 460, 430);
            lv_obj_set_style_bg_color(settings_wifi_page, lv_color_hex(0x172234), 0);
            lv_obj_set_style_bg_opa(settings_wifi_page, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(settings_wifi_page, 0, 0);
            lv_obj_set_style_radius(settings_wifi_page, 18, 0);
            lv_obj_set_style_pad_all(settings_wifi_page, 12, 0);
            lv_obj_set_scrollable(settings_wifi_page, false);
            lv_obj_set_hidden(settings_wifi_page, true);
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
            lv_obj_set_hidden(wifi_networks_label, true);
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
            lv_obj_set_scrollable(wifi_password_page, false);
            lv_obj_set_hidden(wifi_password_page, true);
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
            /* Mask immediately instead of repeatedly showing each typed character. */
            lv_textarea_set_password_show_time(wifi_password_input, 0);
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
            /* Keep the keyboard fixed instead of allowing container scrolling. */
            lv_obj_set_scrollable(wifi_keyboard, false);

        }
    }

    tick_screen_door_cam();
}

void tick_screen_door_cam() {
    screen_settings_tick();
    settings_wifi_refresh();
    ha_settings_refresh();
    system_settings_refresh();
    wifi_password_refresh();
    home_dynamic_tick();
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
            lv_color_t bg = lv_color_hex(detected ? 0xf59e0b : 0x24354b);
            lv_obj_set_style_bg_color(home_camera_button, bg, 0);
            lv_obj_set_style_bg_color(home_camera_button, bg, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(home_camera_button, 0),
                lv_color_hex(detected ? 0x101827 : 0xb9cbe0), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(home_camera_button, 1),
                lv_color_hex(detected ? 0x101827 : 0xffffff), 0);
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
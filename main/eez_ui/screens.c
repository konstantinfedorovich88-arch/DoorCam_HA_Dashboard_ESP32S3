#include <string.h>
#include <time.h>
#include <stdio.h>

#include "screens.h"
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

#if LV_FONT_MONTSERRAT_22
#define HOME_CLOCK_FONT (&lv_font_montserrat_22)
#else
#define HOME_CLOCK_FONT HOME_VALUE_FONT
#endif
static lv_obj_t *home_clock_time;
static lv_obj_t *home_clock_date;

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

static void home_pending_button(lv_obj_t *parent, int x, int y, const char *symbol, const char *title)
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
        home_label(clock, "Дата и время", 16, 8, &ui_font_roboto, 0xb9cbe0);
        home_clock_time = home_label(clock, "--:--", 16, 32, HOME_CLOCK_FONT, 0x4ade80);
        home_clock_date = home_label(clock, "--.--.----", 16, 100, &ui_font_roboto, 0xb9cbe0);
        home_pending_button(parent_obj, 20, 348, LV_SYMBOL_POWER, "Мастер-выключатель");
        home_pending_button(parent_obj, 408, 348, LV_SYMBOL_VIDEO, "Просмотр камеры");
        home_label(parent_obj, "Home Assistant: --     Wi-Fi: --", 16, 447, &ui_font_roboto, 0xb9cbe0);
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
            // SettingsPopup
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.settings_popup = obj;
            lv_obj_set_pos(obj, 190, 50);
            lv_obj_set_size(obj, 420, 380);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x172234), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    objects.obj5 = obj;
                    lv_obj_set_pos(obj, 325, -20);
                    lv_obj_set_size(obj, 58, 27);
                    lv_obj_add_event_cb(obj, action_close_settings, LV_EVENT_CLICKED, (void *)0);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0x212121), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xf00000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "X");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj6 = obj;
                    lv_obj_set_pos(obj, 153, -16);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text_static(obj, "Настройки");
                }
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    objects.obj7 = obj;
                    lv_obj_set_pos(obj, 0, 267);
                    lv_obj_set_size(obj, 374, 50);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0x3cffde), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "Система");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 204);
                    lv_obj_set_size(obj, 374, 50);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj8 = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0x3cffde), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "Home Assistant");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 138);
                    lv_obj_set_size(obj, 374, 50);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj9 = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0x3cffde), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "Звук");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    objects.obj10 = obj;
                    lv_obj_set_pos(obj, 0, 73);
                    lv_obj_set_size(obj, 374, 50);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0x3cffde), LV_PART_MAIN | LV_STATE_DEFAULT);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "Экран");
                        }
                    }
                }
                {
                    lv_obj_t *obj = lv_button_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 13);
                    lv_obj_set_size(obj, 374, 50);
                    {
                        lv_obj_t *parent_obj = obj;
                        {
                            lv_obj_t *obj = lv_label_create(parent_obj);
                            objects.obj11 = obj;
                            lv_obj_set_pos(obj, 0, 0);
                            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                            lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_obj_set_style_text_color(obj, lv_color_hex(0x3cffde), LV_PART_MAIN | LV_STATE_DEFAULT);
                            lv_label_set_text_static(obj, "Wi-Fi");
                        }
                    }
                }
            }
        }
    }
    
    tick_screen_door_cam();
}

void tick_screen_door_cam() {
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
#include "dashboard_ui.h"
#include "dashboard_call.h"
#include "dashboard_wifi.h"
#include "dashboard_ha.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "DASH_UI";
static dash_call_t s_call;
static lv_obj_t *s_overlay;
static lv_obj_t *s_status;
static void make_button(lv_obj_t *parent, const char *text, int x, int y, lv_event_cb_t cb);

static lv_obj_t *s_wifi_panel, *s_wifi_status, *s_wifi_dropdown, *s_wifi_password, *s_wifi_keyboard;
static unsigned s_wifi_generation, s_wifi_count;
static char s_wifi_ssids[DASH_WIFI_MAX_AP][33];

static void wifi_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_wifi_panel) return;
    dash_wifi_snapshot_t snap;
    dash_wifi_snapshot(&snap);
    char status[140];
    snprintf(status, sizeof(status), "Wi-Fi: %s%s%s", snap.status,
             snap.ip[0] ? " IP: " : "", snap.ip);
    /* Avoid invalidating the UI every 700 ms when status is unchanged. */
    if (strcmp(lv_label_get_text(s_wifi_status), status) != 0) {
        lv_label_set_text(s_wifi_status, status);
    }
    if (snap.generation != s_wifi_generation) {
        s_wifi_generation = snap.generation;
        s_wifi_count = snap.count;
        char options[768] = {0};
        size_t used = 0;
        for (unsigned i = 0; i < snap.count; ++i) {
            snprintf(s_wifi_ssids[i], sizeof(s_wifi_ssids[i]), "%s", snap.aps[i].ssid);
            int n = snprintf(options + used, sizeof(options) - used, "%s%s",
                             i ? "\n" : "", snap.aps[i].ssid[0] ? snap.aps[i].ssid : "(hidden)");
            if (n < 0 || (size_t)n >= sizeof(options) - used) break;
            used += (size_t)n;
        }
        lv_dropdown_set_options(s_wifi_dropdown, used ? options : "No networks found");
    }
}
static void wifi_scan_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t err = dash_wifi_scan();
    if (err != ESP_OK) ESP_LOGW(TAG, "Scan: %s", esp_err_to_name(err));
}
static void wifi_connect_cb(lv_event_t *event)
{
    (void)event;
    if (!s_wifi_count) {
        lv_label_set_text(s_wifi_status, "Scan for networks first");
        return;
    }
    unsigned i = lv_dropdown_get_selected(s_wifi_dropdown);
    if (i >= s_wifi_count) return;
    esp_err_t err = dash_wifi_connect(s_wifi_ssids[i], lv_textarea_get_text(s_wifi_password));
    if (err != ESP_OK) {
        lv_label_set_text(s_wifi_status, "Cannot connect; check password");
        ESP_LOGW(TAG, "Connect: %s", esp_err_to_name(err));
    } else {
        lv_obj_set_hidden(s_wifi_keyboard, true);
    }
}
static void wifi_focus_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_set_hidden(s_wifi_keyboard, false);
}
static void wifi_keyboard_done_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_set_hidden(s_wifi_keyboard, true);
}
static void wifi_close_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_delete(s_wifi_panel);
    s_wifi_panel = NULL;
    s_wifi_keyboard = NULL;
}
static void wifi_open_cb(lv_event_t *event)
{
    (void)event;
    if (s_wifi_panel) return;
    s_wifi_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_wifi_panel, 740, 430);
    lv_obj_center(s_wifi_panel);
    lv_obj_t *heading = lv_label_create(s_wifi_panel);
    lv_label_set_text(heading, "Wi-Fi settings (2.4 GHz)");
    lv_obj_set_pos(heading, 20, 10);
    s_wifi_status = lv_label_create(s_wifi_panel);
    lv_obj_set_pos(s_wifi_status, 20, 48);
    lv_label_set_text(s_wifi_status, "Wi-Fi starting...");
    s_wifi_dropdown = lv_dropdown_create(s_wifi_panel);
    lv_obj_set_pos(s_wifi_dropdown, 20, 85);
    lv_obj_set_width(s_wifi_dropdown, 380);
    lv_dropdown_set_options(s_wifi_dropdown, "Tap Scan");
    s_wifi_password = lv_textarea_create(s_wifi_panel);
    lv_obj_set_pos(s_wifi_password, 20, 145);
    lv_obj_set_size(s_wifi_password, 380, 50);
    lv_textarea_set_one_line(s_wifi_password, true);
    lv_textarea_set_password_mode(s_wifi_password, true);
    lv_textarea_set_placeholder_text(s_wifi_password, "Wi-Fi password");
    lv_textarea_set_max_length(s_wifi_password, 63);
    lv_obj_add_event_cb(s_wifi_password, wifi_focus_cb, LV_EVENT_FOCUSED, NULL);
    make_button(s_wifi_panel, "Scan", 430, 85, wifi_scan_cb);
    make_button(s_wifi_panel, "Connect", 430, 145, wifi_connect_cb);
    make_button(s_wifi_panel, "Close", 560, 10, wifi_close_cb);
    s_wifi_keyboard = lv_keyboard_create(s_wifi_panel);
    lv_obj_set_size(s_wifi_keyboard, 700, 195);
    lv_obj_align(s_wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_wifi_keyboard, s_wifi_password);
    lv_obj_add_event_cb(s_wifi_keyboard, wifi_keyboard_done_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wifi_keyboard, wifi_keyboard_done_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_set_hidden(s_wifi_keyboard, true);
    s_wifi_generation = (unsigned)-1;
    wifi_refresh(NULL);
}


static lv_obj_t *s_ha_panel, *s_ha_status;
static lv_timer_t *s_ha_timer;

static void ha_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ha_status) return;
    char status[100];
    dashboard_ha_status(status, sizeof(status));
    if (strcmp(lv_label_get_text(s_ha_status), status) != 0)
        lv_label_set_text(s_ha_status, status);
}

static void ha_close_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_timer) { lv_timer_delete(s_ha_timer); s_ha_timer = NULL; }
    lv_obj_delete(s_ha_panel);
    s_ha_panel = NULL;
    s_ha_status = NULL;
}

static void ha_test_cb(lv_event_t *event)
{
    (void)event;
    dash_wifi_snapshot_t snap;
    dash_wifi_snapshot(&snap);
    if (!snap.connected) {
        lv_label_set_text(s_ha_status, "Connect Wi-Fi first");
        return;
    }
    esp_err_t err = dashboard_ha_probe("192.168.0.215", 8123);
    if (err != ESP_OK) lv_label_set_text(s_ha_status, "HA test already running or unavailable");
    else ha_refresh(NULL);
}

static void ha_open_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_panel) return;
    s_ha_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_ha_panel, 680, 290);
    lv_obj_center(s_ha_panel);
    lv_obj_t *title = lv_label_create(s_ha_panel);
    lv_label_set_text(title, "Home Assistant connection");
    lv_obj_set_pos(title, 20, 12);
    lv_obj_t *address = lv_label_create(s_ha_panel);
    lv_label_set_text(address, "Server: http://192.168.0.215:8123");
    lv_obj_set_pos(address, 20, 62);
    s_ha_status = lv_label_create(s_ha_panel);
    lv_label_set_long_mode(s_ha_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ha_status, 620);
    lv_obj_set_pos(s_ha_status, 20, 110);
    make_button(s_ha_panel, "Test server", 20, 175, ha_test_cb);
    make_button(s_ha_panel, "Close", 465, 175, ha_close_cb);
    s_ha_timer = lv_timer_create(ha_refresh, 800, NULL);
    ha_refresh(NULL);
}

static void add_label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
}

static void close_overlay(void)
{
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
}

static void on_accept(lv_event_t *event)
{
    (void)event;
    if (!dash_call_accept(&s_call, s_call.session_id)) return;
    close_overlay();
    lv_label_set_text(s_status, "DoorCam: call active (UI demo; audio not connected)");
}

static void on_reject(lv_event_t *event)
{
    (void)event;
    if (!dash_call_reject(&s_call, s_call.session_id)) return;
    close_overlay();
    lv_label_set_text(s_status, "DoorCam: call rejected (UI demo)");
}

static void make_button(lv_obj_t *parent, const char *text, int x, int y,
                        lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 150, 48);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

void dashboard_ui_show_incoming_call(void)
{
    if (s_overlay || !dash_call_ring(&s_call, 1)) return; /* UI demo only */
    s_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_overlay, 500, 260);
    lv_obj_center(s_overlay);
    add_label(s_overlay, "DOORCAM - INCOMING CALL", 80, 35);
    add_label(s_overlay, "Audio / video not connected yet", 80, 85);
    make_button(s_overlay, "Reject", 50, 155, on_reject);
    make_button(s_overlay, "Accept", 275, 155, on_accept);
}

static void on_demo_ring(lv_event_t *event)
{
    (void)event;
    ESP_LOGI(TAG, "Test call UI CLICKED");
    dashboard_ui_show_incoming_call();
}

void dashboard_ui_create(void)
{
    dash_call_init(&s_call);
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x152131), 0);
    add_label(root, "HOME ASSISTANT  /  DOORCAM", 28, 25);
    add_label(root, "Rooms        Lights        Climate        Security", 28, 105);
    add_label(root, "Entities will appear after HA pairing", 28, 165);
    s_status = lv_label_create(root);
    lv_label_set_text(s_status, "DoorCam: waiting for integration");
    lv_obj_set_pos(s_status, 28, 250);
    make_button(root, "Test call UI", 28, 330, on_demo_ring);
    make_button(root, "Wi-Fi", 200, 330, wifi_open_cb);
    make_button(root, "Home Assistant", 372, 330, ha_open_cb);
    lv_timer_create(wifi_refresh, 700, NULL);
}

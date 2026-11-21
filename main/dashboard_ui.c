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


static lv_obj_t *s_ha_panel, *s_ha_status, *s_ha_token, *s_ha_keyboard;
static lv_timer_t *s_ha_timer;
static lv_obj_t *s_ha_entities;

static void ha_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ha_status) return;
    char status[100];
    dashboard_ha_status(status, sizeof(status));
    if (strcmp(lv_label_get_text(s_ha_status), status) != 0)
        lv_label_set_text(s_ha_status, status);
    if (s_ha_entities) {
        dashboard_ha_snapshot_t snap;
        dashboard_ha_live_snapshot(&snap);
        char lines[850];
        int used = snprintf(lines, sizeof(lines), "Live: %s (%u entities)\\n",
                            snap.connected ? "connected" : "offline", snap.count);
        for (unsigned i = 0; i < snap.count && used > 0 && used < (int)sizeof(lines); i++) {
            int n = snprintf(lines + used, sizeof(lines) - (size_t)used, "%s: %s\\n",
                             snap.entities[i].entity_id, snap.entities[i].state);
            if (n < 0) break;
            used += n;
        }
        if (strcmp(lv_label_get_text(s_ha_entities), lines) != 0)
            lv_label_set_text(s_ha_entities, lines);
    }
}

static void ha_close_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_timer) { lv_timer_delete(s_ha_timer); s_ha_timer = NULL; }
    lv_obj_delete(s_ha_panel);
    s_ha_panel = NULL;
    s_ha_status = NULL;
    s_ha_token = NULL;
    s_ha_keyboard = NULL;
    s_ha_entities = NULL;
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

static void ha_token_focus_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_keyboard) lv_obj_set_hidden(s_ha_keyboard, false);
}

static void ha_keyboard_done_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_keyboard) lv_obj_set_hidden(s_ha_keyboard, true);
}

static void ha_save_cb(lv_event_t *event)
{
    (void)event;
    const char *token = lv_textarea_get_text(s_ha_token);
    esp_err_t err = dashboard_ha_save_token(token);
    if (err == ESP_OK) {
        lv_textarea_set_text(s_ha_token, "");
        lv_obj_set_hidden(s_ha_keyboard, true);
        ha_refresh(NULL);
    } else {
        lv_label_set_text(s_ha_status, "Invalid token or NVS save failed");
    }
}

static void ha_auth_cb(lv_event_t *event)
{
    (void)event;
    dash_wifi_snapshot_t snap;
    dash_wifi_snapshot(&snap);
    if (!snap.connected) {
        lv_label_set_text(s_ha_status, "Connect Wi-Fi first");
        return;
    }
    esp_err_t err = dashboard_ha_auth_test();
    if (err != ESP_OK)
        lv_label_set_text(s_ha_status, "Save a token first or wait for test");
    else ha_refresh(NULL);
}

static void ha_live_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t err = dashboard_ha_live_start();
    if (err != ESP_OK) lv_label_set_text(s_ha_status, "Cannot start HA live connection");
}

static void ha_open_cb(lv_event_t *event)
{
    (void)event;
    if (s_ha_panel) return;
    s_ha_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_ha_panel, 740, 430);
    lv_obj_center(s_ha_panel);
    lv_obj_t *title = lv_label_create(s_ha_panel);
    lv_label_set_text(title, "Home Assistant connection");
    lv_obj_set_pos(title, 16, 6);
    lv_obj_t *address = lv_label_create(s_ha_panel);
    lv_label_set_text(address, "Server: http://192.168.0.215:8123");
    lv_obj_set_pos(address, 16, 38);
    char setup_code[32];
    dashboard_ha_setup_code(setup_code, sizeof(setup_code));
    char setup_hint[120];
    snprintf(setup_hint, sizeof(setup_hint), "Web setup: http://<ESP32-IP>/  Code: %s", setup_code);
    lv_obj_t *hint = lv_label_create(s_ha_panel);
    lv_label_set_text(hint, setup_hint);
    lv_obj_set_pos(hint, 16, 58);
    s_ha_status = lv_label_create(s_ha_panel);
    lv_label_set_long_mode(s_ha_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ha_status, 680);
    lv_obj_set_pos(s_ha_status, 16, 85);
    s_ha_token = lv_textarea_create(s_ha_panel);
    lv_obj_set_pos(s_ha_token, 16, 112);
    lv_obj_set_size(s_ha_token, 670, 42);
    lv_textarea_set_one_line(s_ha_token, true);
    lv_textarea_set_password_mode(s_ha_token, true);
    lv_textarea_set_max_length(s_ha_token, 511);
    lv_textarea_set_placeholder_text(s_ha_token, "Paste Long-Lived Access Token");
    lv_obj_add_event_cb(s_ha_token, ha_token_focus_cb, LV_EVENT_FOCUSED, NULL);
    make_button(s_ha_panel, "Save token", 16, 165, ha_save_cb);
    make_button(s_ha_panel, "Test auth", 185, 165, ha_auth_cb);
    make_button(s_ha_panel, "Test server", 354, 165, ha_test_cb);
    make_button(s_ha_panel, "Close", 525, 165, ha_close_cb);
    make_button(s_ha_panel, "Live states", 16, 223, ha_live_cb);
    s_ha_entities = lv_label_create(s_ha_panel);
    lv_label_set_long_mode(s_ha_entities, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ha_entities, 480);
    lv_obj_set_pos(s_ha_entities, 185, 223);
    s_ha_keyboard = lv_keyboard_create(s_ha_panel);
    lv_obj_set_size(s_ha_keyboard, 690, 195);
    lv_obj_align(s_ha_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_ha_keyboard, s_ha_token);
    lv_obj_add_event_cb(s_ha_keyboard, ha_keyboard_done_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_ha_keyboard, ha_keyboard_done_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_set_hidden(s_ha_keyboard, true);
    s_ha_timer = lv_timer_create(ha_refresh, 800, NULL);
    ha_refresh(NULL);
    if (dashboard_ha_has_token())
        lv_textarea_set_placeholder_text(s_ha_token, "Token saved; enter only to replace");
}

/* Lightweight entity cards: LVGL is accessed only from the UI task. */
static lv_obj_t *s_cards_panel;
static lv_obj_t *s_card_labels[DASH_HA_MAX_ENTITIES];
static lv_timer_t *s_cards_timer;

static void cards_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_cards_panel) return;
    dashboard_ha_snapshot_t snap;
    dashboard_ha_live_snapshot(&snap);
    for (unsigned i=0; i<DASH_HA_MAX_ENTITIES; i++) {
        if (!s_card_labels[i]) continue;
        lv_obj_t *card=lv_obj_get_parent(s_card_labels[i]);
        bool visible=snap.panel_loaded ? (i<snap.panel_count) : (i<snap.count);
        if (lv_obj_is_hidden(card)==visible)
            lv_obj_set_hidden(card,!visible);
        if (!visible) continue;
        const dashboard_ha_entity_t *entity=snap.panel_loaded ?
            &snap.panel_cards[i] : &snap.entities[i];
        char line[160];
        const char *name=entity->title[0]?entity->title:entity->entity_id;
        if (!strcmp(name,entity->entity_id)) {
            const char *dot=strchr(name,'.');
            if (dot) name=dot+1;
        }
        snprintf(line,sizeof(line),"%s\n%s",name,
                 entity->state[0]?entity->state:"Loading...");
        if (strcmp(lv_label_get_text(s_card_labels[i]),line)!=0)
            lv_label_set_text(s_card_labels[i],line);
    }
}

static void cards_close_cb(lv_event_t *event)
{
    (void)event;
    if (s_cards_timer) { lv_timer_delete(s_cards_timer); s_cards_timer=NULL; }
    if (s_cards_panel) { lv_obj_delete(s_cards_panel); s_cards_panel=NULL; }
    memset(s_card_labels,0,sizeof(s_card_labels));
}

static void cards_open_cb(lv_event_t *event)
{
    (void)event;
    if (s_cards_panel) return;
    (void)dashboard_ha_live_start();
    s_cards_panel=lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_cards_panel,760,435);
    lv_obj_center(s_cards_panel);
    lv_obj_t *title=lv_label_create(s_cards_panel);
    lv_label_set_text(title,"DoorCam Panel - Home Assistant (read only)");
    lv_obj_set_pos(title,14,10);
    make_button(s_cards_panel,"Close",570,2,cards_close_cb);
    for (unsigned i=0;i<DASH_HA_MAX_ENTITIES;i++) {
        lv_obj_t *card=lv_obj_create(s_cards_panel);
        lv_obj_set_size(card,170,140);
        lv_obj_set_pos(card,12+(int)(i%4)*180,65+(int)(i/4)*150);
        s_card_labels[i]=lv_label_create(card);
        lv_label_set_long_mode(s_card_labels[i],LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_card_labels[i],140);
        lv_label_set_text(s_card_labels[i],"Waiting for device...");
        lv_obj_center(s_card_labels[i]);
    }
    s_cards_timer=lv_timer_create(cards_refresh,3000,NULL);
    cards_refresh(NULL);
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

static void on_hangup(lv_event_t *event);

static void on_accept(lv_event_t *event)
{
    (void)event;
    if (!dash_call_accept(&s_call, s_call.session_id)) return;
    close_overlay();
    s_overlay=lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_overlay,500,260);
    lv_obj_center(s_overlay);
    add_label(s_overlay,"DOORCAM - CALL ACTIVE",80,35);
    add_label(s_overlay,"Video / audio hardware not connected",45,85);
    make_button(s_overlay,"Hang up",175,155,on_hangup);
    lv_label_set_text(s_status, "DoorCam: demo call active");
}

static void on_hangup(lv_event_t *event)
{
    (void)event;
    if (!dash_call_hangup(&s_call, s_call.session_id)) return;
    close_overlay();
    lv_label_set_text(s_status, "DoorCam: demo call ended");
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
    make_button(root, "Devices", 544, 330, cards_open_cb);
    lv_timer_create(wifi_refresh, 700, NULL);
}

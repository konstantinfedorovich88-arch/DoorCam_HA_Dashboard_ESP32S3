#include "dashboard_ui.h"
#include "dashboard_call.h"
#include "esp_log.h"

static const char *TAG = "DASH_UI";
static dash_call_t s_call;
static lv_obj_t *s_overlay;
static lv_obj_t *s_status;

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
}

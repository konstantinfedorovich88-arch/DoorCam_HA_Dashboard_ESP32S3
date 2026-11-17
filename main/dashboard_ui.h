#pragma once
#include "lvgl.h"

/* Must be called on the LVGL thread with LVGL initialized. */
void dashboard_ui_create(void);
void dashboard_ui_show_incoming_call(void);

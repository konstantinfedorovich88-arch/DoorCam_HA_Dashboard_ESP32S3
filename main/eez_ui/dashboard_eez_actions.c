#include "actions.h"
#include "screens.h"

void action_open_settings(lv_event_t *e)
{
    (void)e;
    if (objects.settings_popup)
        lv_obj_remove_flag(objects.settings_popup, LV_OBJ_FLAG_HIDDEN);
}

void action_close_settings(lv_event_t *e)
{
    (void)e;
    if (objects.settings_popup)
        lv_obj_add_flag(objects.settings_popup, LV_OBJ_FLAG_HIDDEN);
}

/*
 * Settings screen: full-screen tab view (no header; back = HMI button 1 or
 * swipe right).
 *
 * The encoder group holds: tab buttons, and the widgets of the
 * *active* tab only, so rotating never jumps into an invisible tab. It is
 * rebuilt on tab change and whenever a tab changes its widgets.
 */
#include "ui_priv.h"

#define TAB_BAR_H 34

enum { TAB_DISPLAY, TAB_NETWORK };

static lv_obj_t *s_tabview;
static lv_group_t *s_group;

lv_group_t *ui_settings_group(void)
{
    return s_group;
}

void ui_settings_refresh_group(void)
{
    uint32_t active = lv_tabview_get_tab_active(s_tabview);
    lv_obj_t *page = lv_obj_get_child(lv_tabview_get_content(s_tabview), (int32_t)active);
    lv_obj_t *roots[] = { lv_tabview_get_tab_bar(s_tabview), page };
    ui_group_rebuild(s_group, roots, 2);
}

static void tab_changed_cb(lv_event_t *e)
{
    (void)e;
    ui_settings_refresh_group();
    if (lv_tabview_get_tab_active(s_tabview) == TAB_NETWORK) {
        ui_network_tab_shown();
    }
}

void ui_settings_on_show(void)
{
    ui_settings_refresh_group();
    if (lv_tabview_get_tab_active(s_tabview) == TAB_NETWORK) {
        ui_network_tab_shown();
    }
}

lv_obj_t *ui_settings_create(void)
{
    lv_obj_t *scr = ui_create_screen();
    s_group = lv_group_create();

    ui_enable_back_gesture(scr);

    s_tabview = lv_tabview_create(scr);
    lv_obj_set_size(s_tabview, LV_PCT(100), LV_PCT(100));
    lv_tabview_set_tab_bar_size(s_tabview, TAB_BAR_H);
    lv_obj_set_style_bg_color(s_tabview, UI_COLOR_BG, 0);

    lv_obj_t *bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG, 0);

    /* No swiping between tabs: it fights with the slider and list scrolling,
     * and keeps the encoder group in sync with what's visible. */
    lv_obj_remove_flag(lv_tabview_get_content(s_tabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *display = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EYE_OPEN " Display");
    lv_obj_t *network = lv_tabview_add_tab(s_tabview, LV_SYMBOL_WIFI " Network");
    ui_display_tab_create(display);
    ui_network_tab_create(network);

    lv_obj_add_event_cb(s_tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_settings_refresh_group();
    return scr;
}

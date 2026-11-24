#include "ui_priv.h"

static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

/** Launcher icon: rounded button with a symbol, caption underneath. */
static lv_obj_t *create_app_icon(lv_obj_t *parent, lv_group_t *group, const char *symbol,
                                 const char *name, lv_event_cb_t cb)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_size(cell, 88, 104);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 6, 0);
    /* Leave room for the focus outline. */
    lv_obj_set_style_pad_top(cell, 6, 0);

    lv_obj_t *btn = lv_button_create(cell);
    lv_obj_set_size(btn, 72, 72);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(UI_COLOR_ACCENT, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    ui_add_focusable(group, btn);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_center(icon);

    lv_obj_t *caption = lv_label_create(cell);
    lv_label_set_text(caption, name);
    lv_obj_set_style_text_color(caption, UI_COLOR_TEXT, 0);

    return btn;
}

lv_obj_t *ui_home_create(lv_group_t *group)
{
    lv_obj_t *scr = ui_create_screen();
    lv_obj_t *hdr = ui_create_header(scr, NULL);
    ui_add_clock(hdr);

    /* Grid under the header; flex-wrap so more apps can be added later. */
    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_set_size(grid, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - UI_HEADER_H);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 16, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_app_icon(grid, group, LV_SYMBOL_SETTINGS, "Settings", open_settings_cb);
    return scr;
}

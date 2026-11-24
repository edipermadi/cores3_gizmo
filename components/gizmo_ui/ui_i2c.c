/*
 * "I2C Devices" screen: scans the internal I2C bus and lists responding
 * addresses with known device names.
 *
 * Layout:
 *   [<]        I2C Devices          [status]
 *   [N devices on internal bus ..] [rescan]
 *   [0x34  AXP2101 power                    ]
 *   [0x41  Module HMI                       ]
 *   ...
 *
 * Rows are (inert) list buttons so the encoder can scroll through them.
 */
#include <stdio.h>

#include "ui_priv.h"

#define ROW_H 36

static lv_obj_t *s_header;
static lv_obj_t *s_summary;
static lv_obj_t *s_list;
static lv_obj_t *s_body;
static lv_group_t *s_group;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_show_home();
}

static void rescan(void)
{
    uint8_t addrs[112];
    int n = gizmo_core_i2c_scan(addrs, sizeof(addrs));

    lv_obj_clean(s_list);
    if (n < 0) {
        lv_label_set_text(s_summary, "I2C bus not available");
        lv_obj_set_style_text_color(s_summary, UI_COLOR_LOW, 0);
    } else {
        lv_label_set_text_fmt(s_summary, "%d device%s on internal bus", n, n == 1 ? "" : "s");
        lv_obj_set_style_text_color(s_summary, UI_COLOR_SUBTEXT, 0);
    }

    for (int i = 0; i < n; i++) {
        const char *name = gizmo_core_i2c_device_name(addrs[i]);
        char text[48];
        snprintf(text, sizeof(text), "0x%02X   %s", addrs[i], name ? name : "unknown");

        lv_obj_t *row = lv_list_add_button(s_list, NULL, text);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD, 0);
        lv_obj_set_style_text_color(row, name ? UI_COLOR_TEXT : UI_COLOR_SUBTEXT, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
    }

    lv_obj_t *roots[] = { s_header, s_body };
    ui_group_rebuild(s_group, roots, 2);
}

static void rescan_cb(lv_event_t *e)
{
    (void)e;
    rescan();
}

lv_group_t *ui_i2c_group(void)
{
    return s_group;
}

void ui_i2c_on_show(void)
{
    rescan();
}

lv_obj_t *ui_i2c_create(void)
{
    lv_obj_t *scr = ui_create_screen();
    s_group = lv_group_create();

    s_header = ui_create_header(scr, "I2C");
    ui_create_back_button(s_header, back_cb);

    s_body = lv_obj_create(scr);
    lv_obj_set_size(s_body, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - UI_HEADER_H);
    lv_obj_align(s_body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_radius(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 8, 0);
    lv_obj_set_style_pad_row(s_body, 6, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);

    /* Summary + rescan */
    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_summary = lv_label_create(row);
    lv_obj_set_flex_grow(s_summary, 1);
    lv_label_set_text(s_summary, "");

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, ROW_H, ROW_H);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl);

    /* Device list: sized to content; the body scrolls. */
    s_list = lv_list_create(s_body);
    lv_obj_set_size(s_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);

    lv_obj_t *roots[] = { s_header, s_body };
    ui_group_rebuild(s_group, roots, 2);
    return scr;
}

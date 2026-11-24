/*
 * "Diagnostics" screen: full-screen tab view (no header; back = HMI button 1
 * or swipe right).
 *
 *   I2C    internal-bus scan: responding addresses with known device names,
 *          rescanned each time the tab is shown and on demand.
 *   Power  AXP2101 readings (battery/USB/system voltages, charge state,
 *          die temperature), refreshed every second while visible.
 *
 * As in Settings, the encoder group holds the tab buttons and the active
 * tab's widgets only, rebuilt when the tab or its list changes.
 */
#include <stdio.h>

#include "ui_priv.h"

#define TAB_BAR_H 34
#define ROW_H     36
#define POWER_TICK_MS 1000

enum { TAB_I2C, TAB_POWER };

static lv_obj_t *s_scr;
static lv_obj_t *s_tabview;
static lv_group_t *s_group;

/* I2C tab */
static lv_obj_t *s_i2c_summary;
static lv_obj_t *s_i2c_list;

/* Power tab: value labels, one per row */
enum {
    PW_SOURCE, PW_BATTERY, PW_STATE, PW_ILIM, PW_VBAT, PW_VBUS, PW_VSYS, PW_TEMP, PW_STATUS, PW_COUNT
};
static const char *const s_pw_names[PW_COUNT] = {
    "Power source", "Battery", "Charging", "Charge current (set)", "Battery voltage", "USB voltage",
    "System voltage", "PMU temperature", "Status regs",
};
static lv_obj_t *s_pw_values[PW_COUNT];

/* ---------- shared ---------- */

static lv_obj_t *active_page(void)
{
    uint32_t active = lv_tabview_get_tab_active(s_tabview);
    return lv_obj_get_child(lv_tabview_get_content(s_tabview), (int32_t)active);
}

static void refresh_group(void)
{
    lv_obj_t *roots[] = { lv_tabview_get_tab_bar(s_tabview), active_page() };
    ui_group_rebuild(s_group, roots, 2);
}

static lv_obj_t *transparent_row(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static void page_column(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 8, 0);
    /* Keep content clear of the vertical scrollbar on the right edge. */
    lv_obj_set_style_pad_right(page, 16, 0);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
}

/* ---------- I2C tab ---------- */

static void i2c_rescan(void)
{
    uint8_t addrs[112];
    int n = gizmo_core_i2c_scan(addrs, sizeof(addrs));

    lv_obj_clean(s_i2c_list);
    if (n < 0) {
        lv_label_set_text(s_i2c_summary, "I2C bus not available");
        lv_obj_set_style_text_color(s_i2c_summary, UI_COLOR_LOW, 0);
    } else {
        lv_label_set_text_fmt(s_i2c_summary, "%d device%s on internal bus", n, n == 1 ? "" : "s");
        lv_obj_set_style_text_color(s_i2c_summary, UI_COLOR_SUBTEXT, 0);
    }

    for (int i = 0; i < n; i++) {
        const char *name = gizmo_core_i2c_device_name(addrs[i]);
        char text[48];
        snprintf(text, sizeof(text), "0x%02X   %s", addrs[i], name ? name : "unknown");

        /* Inert list buttons so the encoder can scroll through the rows. */
        lv_obj_t *row = lv_list_add_button(s_i2c_list, NULL, text);
        lv_obj_set_style_bg_color(row, UI_COLOR_CARD, 0);
        lv_obj_set_style_text_color(row, name ? UI_COLOR_TEXT : UI_COLOR_SUBTEXT, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
    }
    refresh_group();
}

static void i2c_rescan_cb(lv_event_t *e)
{
    (void)e;
    i2c_rescan();
}

static void i2c_tab_create(lv_obj_t *page)
{
    page_column(page);

    lv_obj_t *row = transparent_row(page, ROW_H);
    s_i2c_summary = lv_label_create(row);
    lv_obj_set_flex_grow(s_i2c_summary, 1);
    lv_label_set_text(s_i2c_summary, "");

    lv_obj_t *btn = lv_button_create(row);
    lv_obj_set_size(btn, ROW_H, ROW_H);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    /* Card-coloured button: the base theme's white button text would vanish in light mode. */
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, i2c_rescan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl);

    /* Sized to content; the page scrolls. */
    s_i2c_list = lv_list_create(page);
    lv_obj_set_size(s_i2c_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(s_i2c_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_i2c_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_i2c_list, 0, 0);
    lv_obj_set_style_pad_all(s_i2c_list, 0, 0);
    lv_obj_set_style_pad_row(s_i2c_list, 4, 0);
}

/* ---------- Power tab ---------- */

static void set_mv(lv_obj_t *lbl, int mv)
{
    if (mv < 0) {
        lv_label_set_text(lbl, "--");
    } else {
        lv_label_set_text_fmt(lbl, "%d.%03d V", mv / 1000, mv % 1000);
    }
}

static void power_refresh(void)
{
    gizmo_pmu_info_t p;
    if (gizmo_core_get_pmu_info(&p) != ESP_OK) {
        for (int i = 0; i < PW_COUNT; i++) {
            lv_label_set_text(s_pw_values[i], "--");
        }
        lv_label_set_text(s_pw_values[PW_SOURCE], "PMU not readable");
        return;
    }
    const gizmo_battery_t *b = &p.battery;

    lv_label_set_text(s_pw_values[PW_SOURCE], b->external_power ? "USB" : "Battery");
    if (!b->present) {
        lv_label_set_text(s_pw_values[PW_BATTERY], "Not connected");
    } else if (b->percent >= 0) {
        lv_label_set_text_fmt(s_pw_values[PW_BATTERY], "%d%%", b->percent);
    } else {
        lv_label_set_text(s_pw_values[PW_BATTERY], "Present");
    }
    lv_label_set_text(s_pw_values[PW_STATE], p.charge_phase ? p.charge_phase : "--");
    lv_obj_set_style_text_color(s_pw_values[PW_STATE], b->charging ? UI_COLOR_OK : UI_COLOR_TEXT, 0);
    if (p.charge_limit_ma >= 0) {
        lv_label_set_text_fmt(s_pw_values[PW_ILIM], "%d mA", p.charge_limit_ma);
    } else {
        lv_label_set_text(s_pw_values[PW_ILIM], "--");
    }
    set_mv(s_pw_values[PW_VBAT], p.vbat_mv);
    set_mv(s_pw_values[PW_VBUS], p.vbus_mv);
    set_mv(s_pw_values[PW_VSYS], p.vsys_mv);
    if (p.die_temp_valid) {
        int t10 = (int)(p.die_temp_c * 10.0f + (p.die_temp_c >= 0 ? 0.5f : -0.5f));
        lv_label_set_text_fmt(s_pw_values[PW_TEMP], "%d.%d C", t10 / 10, (t10 < 0 ? -t10 : t10) % 10);
    } else {
        lv_label_set_text(s_pw_values[PW_TEMP], "--");
    }
    lv_label_set_text_fmt(s_pw_values[PW_STATUS], "%02X %02X", p.status1, p.status2);
}

static void power_tick_cb(lv_timer_t *t)
{
    (void)t;
    /* Only while the Power tab is on screen; the data itself is cached. */
    if (lv_screen_active() == s_scr && lv_tabview_get_tab_active(s_tabview) == TAB_POWER) {
        power_refresh();
    }
}

static void power_tab_create(lv_obj_t *page)
{
    page_column(page);
    lv_obj_set_style_pad_row(page, 2, 0);

    for (int i = 0; i < PW_COUNT; i++) {
        lv_obj_t *row = transparent_row(page, 22);
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, s_pw_names[i]);
        lv_obj_set_style_text_color(name, UI_COLOR_SUBTEXT, 0);
        lv_obj_set_flex_grow(name, 1);

        s_pw_values[i] = lv_label_create(row);
        lv_label_set_text(s_pw_values[i], "--");
    }
    lv_timer_create(power_tick_cb, POWER_TICK_MS, NULL);
}

/* ---------- screen ---------- */

static void on_tab_shown(void)
{
    switch (lv_tabview_get_tab_active(s_tabview)) {
    case TAB_I2C:
        i2c_rescan();   /* also rebuilds the group */
        break;
    case TAB_POWER:
        power_refresh();
        refresh_group();
        break;
    default:
        refresh_group();
        break;
    }
}

static void tab_changed_cb(lv_event_t *e)
{
    (void)e;
    on_tab_shown();
}

lv_group_t *ui_diag_group(void)
{
    return s_group;
}

void ui_diag_on_show(void)
{
    on_tab_shown();
}

lv_obj_t *ui_diag_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();

    ui_enable_back_gesture(s_scr);

    s_tabview = lv_tabview_create(s_scr);
    lv_obj_set_size(s_tabview, LV_PCT(100), LV_PCT(100));
    lv_tabview_set_tab_bar_size(s_tabview, TAB_BAR_H);
    lv_obj_set_style_bg_color(s_tabview, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_bar(s_tabview), UI_COLOR_BG, 0);
    /* No swiping between tabs; keeps the encoder group in sync with the view. */
    lv_obj_remove_flag(lv_tabview_get_content(s_tabview), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *i2c = lv_tabview_add_tab(s_tabview, LV_SYMBOL_LIST " I2C");
    lv_obj_t *power = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Power");
    i2c_tab_create(i2c);
    power_tab_create(power);

    lv_obj_add_event_cb(s_tabview, tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    refresh_group();
    return s_scr;
}

/*
 * "Time" tab: timezone dropdown plus the current local time and its source.
 *
 * Encoder: focus the dropdown, push to open, rotate to pick, push to apply.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_time";

#define TICK_MS 1000

static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_source_label;

static lv_obj_t *card(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(100), h);
    lv_obj_set_style_bg_color(c, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/** "UTC+05:30" style prefix from an offset in minutes. */
static void format_offset(char *buf, size_t len, int min)
{
    char sign = min < 0 ? '-' : '+';
    min = abs(min);
    snprintf(buf, len, "UTC%c%02d:%02d", sign, min / 60, min % 60);
}

/** Newline-separated dropdown options, built once from the core's table. */
static char *build_options(void)
{
    int n = gizmo_core_tz_count();
    size_t cap = 0;
    for (int i = 0; i < n; i++) {
        cap += strlen(gizmo_core_tz_name(i)) + 12;   /* "UTC+hh:mm " + '\n' */
    }
    char *opts = malloc(cap + 1);
    if (!opts) {
        return NULL;
    }

    char *p = opts;
    for (int i = 0; i < n; i++) {
        char off[12];
        format_offset(off, sizeof(off), gizmo_core_tz_offset_min(i));
        p += sprintf(p, "%s%s %s", i ? "\n" : "", off, gizmo_core_tz_name(i));
    }
    return opts;
}

static void refresh(void)
{
    if (!gizmo_core_time_is_valid()) {
        lv_label_set_text(s_time_label, "--:--:--");
        lv_label_set_text(s_date_label, "");
        lv_label_set_text(s_source_label, "Not set - connect to Wi-Fi");
        lv_obj_set_style_text_color(s_source_label, UI_COLOR_LOW, 0);
        return;
    }

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    lv_label_set_text(s_time_label, buf);
    strftime(buf, sizeof(buf), "%a %d %b %Y  %Z", &tm);
    lv_label_set_text(s_date_label, buf);

    bool ntp = gizmo_core_time_is_ntp_synced();
    lv_label_set_text(s_source_label, ntp ? LV_SYMBOL_WIFI " Synced via NTP" : "From RTC (not synced yet)");
    lv_obj_set_style_text_color(s_source_label, ntp ? UI_COLOR_OK : UI_COLOR_SUBTEXT, 0);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void tz_changed_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    int idx = (int)lv_dropdown_get_selected(dd);
    esp_err_t err = gizmo_core_tz_set(idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save timezone: %s", esp_err_to_name(err));
    }
    refresh();   /* show the new local time immediately */
}

void ui_time_tab_create(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 8, 0);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* Timezone picker */
    lv_obj_t *tz_card = card(page, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tz_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tz_card, 4, 0);

    lv_obj_t *title = lv_label_create(tz_card);
    lv_label_set_text(title, LV_SYMBOL_GPS "  Timezone");

    lv_obj_t *dd = lv_dropdown_create(tz_card);
    lv_obj_set_width(dd, LV_PCT(100));
    char *opts = build_options();
    if (opts) {
        lv_dropdown_set_options(dd, opts);   /* LVGL copies the string */
        free(opts);
    }
    int cur = gizmo_core_tz_get();
    if (cur >= 0) {
        lv_dropdown_set_selected(dd, (uint32_t)cur);
    }
    lv_obj_set_style_bg_color(dd, UI_COLOR_BG, 0);
    lv_obj_add_event_cb(dd, tz_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Current time */
    lv_obj_t *now_card = card(page, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(now_card, 1);
    lv_obj_set_flex_flow(now_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(now_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(now_card, 2, 0);

    s_time_label = lv_label_create(now_card);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_28, 0);

    s_date_label = lv_label_create(now_card);
    lv_obj_set_style_text_color(s_date_label, UI_COLOR_SUBTEXT, 0);

    s_source_label = lv_label_create(now_card);

    refresh();
    lv_timer_create(tick_cb, TICK_MS, NULL);
}

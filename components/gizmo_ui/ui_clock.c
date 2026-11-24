/*
 * "Clock" screen: full-screen clock with two faces and two formats.
 *
 * Full screen (no header; back = HMI button 1 or swipe right):
 *        analog face  or  13:45 :07
 *                         Tue 24 Nov 2026
 *   [ Analog | Digital ]  [ 24h | 12h ]
 *
 * Face style and 24h/12h are saved preferences ("clk_style", "clk_24h"); the
 * 24h/12h choice also applies to the home top-bar clock. The page ticks only
 * while shown. Time comes from the system clock (RTC at boot, NTP once online).
 */
#include <stdio.h>
#include <time.h>

#include "ui_priv.h"

#define PREF_STYLE   "clk_style"   /* 0 digital, 1 analog */
#define TICK_MS      200
#define FACE_Y       0
#define FACE_H       194
#define BTN_H        34
#define DIAL         176

static lv_obj_t *s_scr;
static lv_group_t *s_group;
static lv_timer_t *s_timer;
static bool s_analog;
static int s_last_sec = -1;

static lv_obj_t *s_digital;          /* digital face container */
static lv_obj_t *s_time_lbl;
static lv_obj_t *s_ampm_lbl;
static lv_obj_t *s_sec_lbl;
static lv_obj_t *s_date_lbl;

static lv_obj_t *s_scale;            /* analog face */
static lv_obj_t *s_hand_h, *s_hand_m, *s_hand_s;

static lv_obj_t *s_style_lbl;
static lv_obj_t *s_fmt_lbl;

static const char *s_dial_labels[] = {
    "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", NULL,
};

/* ---------- update ---------- */

static void update_buttons(void)
{
    lv_label_set_text(s_style_lbl, s_analog ? LV_SYMBOL_LOOP "  Analog" : LV_SYMBOL_LOOP "  Digital");
    lv_label_set_text(s_fmt_lbl, ui_clock_24h() ? LV_SYMBOL_LOOP "  24h" : LV_SYMBOL_LOOP "  12h");
}

static void refresh(bool force)
{
    bool valid = gizmo_core_time_is_valid();
    struct tm tm = { 0 };
    if (valid) {
        time_t now = time(NULL);
        localtime_r(&now, &tm);
    }
    if (!force && valid && tm.tm_sec == s_last_sec) {
        return;
    }
    s_last_sec = tm.tm_sec;

    if (s_analog) {
        int sec = valid ? tm.tm_sec : 0;
        int min = valid ? tm.tm_min : 0;
        int hour_pos = valid ? (tm.tm_hour % 12) * 5 + tm.tm_min / 12 : 0;   /* hour hand creeps */
        lv_scale_set_line_needle_value(s_scale, s_hand_h, DIAL / 2 - 34, hour_pos);
        lv_scale_set_line_needle_value(s_scale, s_hand_m, DIAL / 2 - 16, min);
        lv_scale_set_line_needle_value(s_scale, s_hand_s, DIAL / 2 - 10, sec);
        return;
    }

    if (!valid) {
        lv_label_set_text(s_time_lbl, "--:--");
        lv_label_set_text(s_sec_lbl, "");
        lv_label_set_text(s_ampm_lbl, "");
        lv_label_set_text(s_date_lbl, "Time not set");
        return;
    }
    char buf[32];
    ui_format_hm(buf, sizeof(buf), &tm, false);
    lv_label_set_text(s_time_lbl, buf);
    lv_label_set_text_fmt(s_sec_lbl, ":%02d", tm.tm_sec);
    lv_label_set_text(s_ampm_lbl, ui_clock_24h() ? "" : (tm.tm_hour < 12 ? "AM" : "PM"));
    strftime(buf, sizeof(buf), "%a %d %b %Y", &tm);
    lv_label_set_text(s_date_lbl, buf);
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    refresh(false);
}

static void apply_style(void)
{
    if (s_analog) {
        lv_obj_add_flag(s_digital, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_scale, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_scale, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_digital, LV_OBJ_FLAG_HIDDEN);
    }
    update_buttons();
    refresh(true);
}

/* ---------- events ---------- */

static void style_cb(lv_event_t *e)
{
    (void)e;
    s_analog = !s_analog;
    gizmo_core_pref_set_u8(PREF_STYLE, s_analog);
    apply_style();
}

static void format_cb(lv_event_t *e)
{
    (void)e;
    ui_clock_set_24h(!ui_clock_24h());
    update_buttons();
    refresh(true);
}

/* ---------- build ---------- */

static lv_obj_t *hand(lv_obj_t *scale, int width, lv_color_t color)
{
    lv_obj_t *l = lv_line_create(scale);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

static void build_analog(lv_obj_t *parent)
{
    s_scale = lv_scale_create(parent);
    lv_obj_set_size(s_scale, DIAL, DIAL);
    lv_obj_align(s_scale, LV_ALIGN_CENTER, 0, 0);
    lv_scale_set_mode(s_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(s_scale, 0, 60);
    lv_scale_set_angle_range(s_scale, 360);
    lv_scale_set_rotation(s_scale, 270);            /* 0 at 12 o'clock */
    lv_scale_set_total_tick_count(s_scale, 61);
    lv_scale_set_major_tick_every(s_scale, 5);
    lv_scale_set_label_show(s_scale, true);
    lv_scale_set_text_src(s_scale, s_dial_labels);

    lv_obj_set_style_bg_opa(s_scale, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scale, UI_COLOR_CARD, 0);
    lv_obj_set_style_radius(s_scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_scale, true, 0);
    lv_obj_set_style_arc_width(s_scale, 0, LV_PART_MAIN);
    /* Major ticks + numbers */
    lv_obj_set_style_length(s_scale, 8, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_scale, 2, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_scale, UI_COLOR_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_scale, UI_COLOR_TEXT, LV_PART_INDICATOR);
    /* Minute ticks */
    lv_obj_set_style_length(s_scale, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s_scale, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s_scale, UI_COLOR_SUBTEXT, LV_PART_ITEMS);

    s_hand_h = hand(s_scale, 5, UI_COLOR_TEXT);
    s_hand_m = hand(s_scale, 3, UI_COLOR_TEXT);
    s_hand_s = hand(s_scale, 1, UI_COLOR_LOW);

    lv_obj_t *hub = lv_obj_create(s_scale);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 8, 8);
    lv_obj_center(hub);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(hub, UI_COLOR_LOW, 0);
}

static void build_digital(lv_obj_t *parent)
{
    s_digital = lv_obj_create(parent);
    lv_obj_remove_style_all(s_digital);
    lv_obj_set_size(s_digital, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(s_digital, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(s_digital, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_digital, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_digital, 4, 0);
    lv_obj_remove_flag(s_digital, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* [ 13:45 ][ PM / :07 ] */
    lv_obj_t *row = lv_obj_create(s_digital);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_time_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_48, 0);

    lv_obj_t *side = lv_obj_create(row);
    lv_obj_remove_style_all(side);
    lv_obj_set_size(side, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_bottom(side, 8, 0);
    lv_obj_remove_flag(side, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_ampm_lbl = lv_label_create(side);
    lv_obj_set_style_text_color(s_ampm_lbl, UI_COLOR_SUBTEXT, 0);
    s_sec_lbl = lv_label_create(side);
    lv_obj_set_style_text_font(s_sec_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_sec_lbl, UI_COLOR_SUBTEXT, 0);

    s_date_lbl = lv_label_create(s_digital);
    lv_obj_set_style_text_color(s_date_lbl, UI_COLOR_SUBTEXT, 0);
}

static lv_obj_t *option_button(lv_obj_t *parent, lv_event_cb_t cb, lv_obj_t **label)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, BTN_H);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    /* Card-coloured button: the base theme's white button text would vanish in light mode. */
    lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    *label = lv_label_create(btn);
    lv_obj_center(*label);
    return btn;
}

/* ---------- public ---------- */

lv_group_t *ui_clock_group(void)
{
    return s_group;
}

void ui_clock_on_show(void)
{
    s_last_sec = -1;
    apply_style();
    lv_timer_resume(s_timer);
}

void ui_clock_on_hide(void)
{
    lv_timer_pause(s_timer);
}

lv_obj_t *ui_clock_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();
    s_analog = gizmo_core_pref_get_u8(PREF_STYLE, 0) != 0;

    ui_enable_back_gesture(s_scr);

    lv_obj_t *face = lv_obj_create(s_scr);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, LV_PCT(100), FACE_H);
    lv_obj_set_pos(face, 0, FACE_Y);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    build_analog(face);
    build_digital(face);

    lv_obj_t *bar = lv_obj_create(s_scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), BTN_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    option_button(bar, style_cb, &s_style_lbl);
    option_button(bar, format_cb, &s_fmt_lbl);

    lv_obj_t *roots[] = { bar };
    ui_group_rebuild(s_group, roots, 1);

    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_pause(s_timer);
    apply_style();
    return s_scr;
}

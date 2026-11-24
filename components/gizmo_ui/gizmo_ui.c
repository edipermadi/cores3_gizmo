#include "gizmo_ui.h"

#include "esp_log.h"
#include "gizmo_core.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define COLOR_BG        lv_color_hex(0x101418)
#define COLOR_HEADER    lv_color_hex(0x1c232b)
#define COLOR_ACCENT    lv_color_hex(0x2f80ed)
#define COLOR_TEXT      lv_color_hex(0xe8edf2)
#define COLOR_SUBTEXT   lv_color_hex(0x8a97a6)
#define COLOR_CARD      lv_color_hex(0x1c232b)

#define HEADER_H        40
#define ANIM_MS         200

/* Slider works in steps so one encoder click changes brightness by 5%. */
#define BRIGHTNESS_STEP 5

static lv_obj_t *s_home_scr;
static lv_obj_t *s_settings_scr;
static lv_obj_t *s_brightness_label;
static lv_obj_t *s_brightness_slider;

/* One focus group per screen; the encoder is pointed at the visible one. */
static lv_group_t *s_home_group;
static lv_group_t *s_settings_group;

static lv_style_t s_focus_style;

/* ---------- helpers ---------- */

static lv_obj_t *create_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, COLOR_TEXT, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static lv_obj_t *create_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), HEADER_H);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, COLOR_HEADER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(hdr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return hdr;
}

/** Make obj reachable with the encoder and show a clear outline when focused. */
static void make_focusable(lv_obj_t *obj, lv_group_t *group)
{
    lv_obj_add_style(obj, &s_focus_style, LV_STATE_FOCUS_KEY);
    lv_group_add_obj(group, obj);
}

static void init_focus_style(void)
{
    lv_style_init(&s_focus_style);
    lv_style_set_outline_color(&s_focus_style, COLOR_TEXT);
    lv_style_set_outline_width(&s_focus_style, 2);
    lv_style_set_outline_pad(&s_focus_style, 3);
    lv_style_set_outline_opa(&s_focus_style, LV_OPA_COVER);
}

/* ---------- navigation ---------- */

static void show_screen(lv_obj_t *scr, lv_group_t *group, lv_screen_load_anim_t anim)
{
    lv_indev_t *enc = gizmo_core_get_encoder();
    if (enc) {
        lv_group_set_editing(group, false);
        lv_indev_set_group(enc, group);
    }
    lv_screen_load_anim(scr, anim, ANIM_MS, 0, false);
}

static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    lv_group_focus_obj(s_brightness_slider);
    show_screen(s_settings_scr, s_settings_group, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

static void go_home(void)
{
    /* Persist here too: leaving with the encoder's back button skips the
     * slider's release event. */
    esp_err_t err = gizmo_core_set_brightness(gizmo_core_get_brightness(), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
    }
    show_screen(s_home_scr, s_home_group, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

static void back_home_cb(lv_event_t *e)
{
    (void)e;
    go_home();
}

/** Module HMI button 1 sends ESC: treat it as "back" anywhere on settings. */
static void settings_key_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC) {
        go_home();
    }
}

/* ---------- home screen ---------- */

static lv_obj_t *create_app_icon(lv_obj_t *parent, const char *symbol, const char *name,
                                 lv_event_cb_t cb)
{
    /* Transparent column container: icon button on top, caption below. */
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
    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(COLOR_ACCENT, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    make_focusable(btn, s_home_group);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
    lv_obj_center(icon);

    lv_obj_t *caption = lv_label_create(cell);
    lv_label_set_text(caption, name);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(caption, COLOR_TEXT, 0);

    return btn;
}

static void build_home(void)
{
    s_home_scr = create_screen();
    s_home_group = lv_group_create();
    create_header(s_home_scr, "CoreS3 Gizmo");

    /* Grid area under the header; flex-wrap so more apps can be added later. */
    lv_obj_t *grid = lv_obj_create(s_home_scr);
    lv_obj_set_size(grid, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - HEADER_H);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 16, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_app_icon(grid, LV_SYMBOL_SETTINGS, "Settings", open_settings_cb);
}

/* ---------- settings screen ---------- */

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider) * BRIGHTNESS_STEP;

    lv_label_set_text_fmt(s_brightness_label, "%d%%", value);
    gizmo_core_set_brightness(value, false);
}

static void brightness_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider) * BRIGHTNESS_STEP;

    /* Save only on release (touch lift, or encoder press leaving edit mode)
     * to avoid wearing flash while adjusting. */
    esp_err_t err = gizmo_core_set_brightness(value, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
    }
}

static void build_settings(void)
{
    /* Snap the stored value to the slider's step. */
    int steps = (gizmo_core_get_brightness() + BRIGHTNESS_STEP / 2) / BRIGHTNESS_STEP;
    int initial_brightness = steps * BRIGHTNESS_STEP;

    s_settings_scr = create_screen();
    s_settings_group = lv_group_create();
    lv_obj_t *hdr = create_header(s_settings_scr, "Settings");

    lv_obj_t *back = lv_button_create(hdr);
    lv_obj_set_size(back, 56, HEADER_H - 10);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(back, COLOR_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(back, settings_key_cb, LV_EVENT_KEY, NULL);
    make_focusable(back, s_settings_group);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(back_lbl);

    /* Brightness card */
    lv_obj_t *card = lv_obj_create(s_settings_scr);
    lv_obj_set_size(card, lv_display_get_horizontal_resolution(NULL) - 24, 96);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, HEADER_H + 12);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_hor(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_EYE_OPEN "  Brightness");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_brightness_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_brightness_label, COLOR_SUBTEXT, 0);
    lv_label_set_text_fmt(s_brightness_label, "%d%%", initial_brightness);
    lv_obj_align(s_brightness_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(card);
    s_brightness_slider = slider;
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_slider_set_range(slider, GIZMO_BRIGHTNESS_MIN / BRIGHTNESS_STEP,
                        GIZMO_BRIGHTNESS_MAX / BRIGHTNESS_STEP);
    lv_slider_set_value(slider, steps, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, COLOR_ACCENT, LV_PART_KNOB);
    /* Larger touch target for the knob. */
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, settings_key_cb, LV_EVENT_KEY, NULL);
    make_focusable(slider, s_settings_group);
}

/* ---------- public ---------- */

void gizmo_ui_init(void)
{
    /* Dark theme so widget text defaults to a light colour. The light theme
     * sets dark-grey text on every container, which overrides the screen's
     * inherited text colour and makes labels unreadable on our dark UI. */
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, COLOR_ACCENT,
                                              lv_palette_main(LV_PALETTE_RED),
                                              true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
    init_focus_style();

    build_home();
    build_settings();

    lv_indev_t *enc = gizmo_core_get_encoder();
    if (enc) {
        lv_indev_set_group(enc, s_home_group);
    }
    lv_screen_load(s_home_scr);
}

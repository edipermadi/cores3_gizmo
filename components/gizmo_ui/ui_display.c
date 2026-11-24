#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_display";

/* Slider works in steps so one encoder click changes brightness by 5%. */
#define BRIGHTNESS_STEP 5

static lv_obj_t *s_value_label;

static void brightness_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int value = lv_slider_get_value(slider) * BRIGHTNESS_STEP;

    lv_label_set_text_fmt(s_value_label, "%d%%", value);
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

static void dark_mode_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool dark = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (dark != ui_dark_mode()) {
        ESP_LOGI(TAG, "Switching to %s mode (restart)", dark ? "dark" : "light");
        ui_set_dark_mode_and_restart(dark);
    }
}

/** Card: [moon] Dark mode ......... [switch]. Changing it restarts the device. */
static void create_theme_card(lv_obj_t *page)
{
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_size(card, LV_PCT(100), 56);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_hor(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_EYE_CLOSE "  Dark mode");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(card);
    lv_obj_set_size(sw, 52, 28);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, UI_COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (ui_dark_mode()) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    /* Keep gesture bubbling (unlike the slider): a swipe that starts on the
     * switch is then taken as "back" and LVGL drops the tap, instead of the
     * release toggling dark mode (and restarting the device). */
    lv_obj_add_event_cb(sw, dark_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ui_display_tab_create(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 12, 0);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* Snap the stored value to the slider's step. */
    int steps = (gizmo_core_get_brightness() + BRIGHTNESS_STEP / 2) / BRIGHTNESS_STEP;

    lv_obj_t *card = lv_obj_create(page);
    lv_obj_set_size(card, LV_PCT(100), 88);
    lv_obj_set_style_bg_color(card, UI_COLOR_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_hor(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  Brightness");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_value_label = lv_label_create(card);
    lv_obj_set_style_text_color(s_value_label, UI_COLOR_SUBTEXT, 0);
    lv_label_set_text_fmt(s_value_label, "%d%%", steps * BRIGHTNESS_STEP);
    lv_obj_align(s_value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_slider_set_range(slider, GIZMO_BRIGHTNESS_MIN / BRIGHTNESS_STEP,
                        GIZMO_BRIGHTNESS_MAX / BRIGHTNESS_STEP);
    lv_slider_set_value(slider, steps, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_KNOB);
    /* Larger touch target for the knob. */
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    /* Horizontal drags must not be taken as the "back" swipe. */
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, brightness_released_cb, LV_EVENT_RELEASED, NULL);

    create_theme_card(page);
}

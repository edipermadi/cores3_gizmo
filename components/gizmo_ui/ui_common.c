#include <string.h>
#include <time.h>

#include "ui_priv.h"

/* ---------- theme ---------- */

static lv_style_t s_focus_style;

void ui_add_focusable(lv_group_t *group, lv_obj_t *obj)
{
    /* Remove first so rebuilding a group never stacks duplicate styles. */
    lv_obj_remove_style(obj, &s_focus_style, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(obj, &s_focus_style, LV_STATE_FOCUS_KEY);
    lv_group_add_obj(group, obj);
}

void ui_theme_init(void)
{
    lv_style_init(&s_focus_style);
    lv_style_set_outline_color(&s_focus_style, UI_COLOR_TEXT);
    lv_style_set_outline_width(&s_focus_style, 2);
    lv_style_set_outline_pad(&s_focus_style, 2);
    lv_style_set_outline_opa(&s_focus_style, LV_OPA_COVER);

    /* Dark theme so widget text defaults to a light colour. The light theme
     * sets dark-grey text on every container, which overrides the screen's
     * inherited text colour. */
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, UI_COLOR_ACCENT,
                                              lv_palette_main(LV_PALETTE_RED),
                                              true, LV_FONT_DEFAULT);
    lv_display_set_theme(disp, theme);
}

lv_obj_t *ui_create_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, UI_COLOR_TEXT, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

/* ---------- status bar: battery + Wi-Fi ---------- */

#define BATTERY_LOW_PCT 15
#define STATUS_TICK_MS  500 /* status refresh + charging animation frame */
#define MAX_STATUS      4   /* one per header */

/** Icons in one header: [wifi] [charge bolt] [battery] [NN%] */
typedef struct {
    lv_obj_t *wifi;
    lv_obj_t *charge;
    lv_obj_t *icon;
    lv_obj_t *pct;
} status_widgets_t;

static status_widgets_t s_status[MAX_STATUS];
static int s_status_count;
static int s_anim_frame;

static const char *const s_battery_symbols[] = {
    LV_SYMBOL_BATTERY_EMPTY, LV_SYMBOL_BATTERY_1, LV_SYMBOL_BATTERY_2,
    LV_SYMBOL_BATTERY_3, LV_SYMBOL_BATTERY_FULL,
};
#define BATTERY_LEVELS ((int)(sizeof(s_battery_symbols) / sizeof(s_battery_symbols[0])))

/** Icon level 0..BATTERY_LEVELS-1 for a charge percentage. */
static int battery_level(int percent)
{
    if (percent >= 90) return 4;
    if (percent >= 65) return 3;
    if (percent >= 40) return 2;
    if (percent >= BATTERY_LOW_PCT) return 1;
    return 0;
}

/**
 * Icon to show this frame. While charging, fill up from the current level to
 * full and repeat, so the animation also hints at how charged it is.
 */
static const char *battery_icon(const gizmo_battery_t *b)
{
    int level = battery_level(b->percent < 0 ? 0 : b->percent);
    if (b->charging) {
        int span = BATTERY_LEVELS - level;   /* frames from current level to full */
        if (span < 2) {
            span = 2;                         /* at full: blink between 3/4 and full */
            level = BATTERY_LEVELS - 2;
        }
        level += s_anim_frame % span;
    }
    return s_battery_symbols[level];
}

static void battery_apply(const status_widgets_t *w, const gizmo_battery_t *b, bool ok)
{
    if (!ok || !b->present) {
        /* No battery (or PMIC unreadable): show only that we're on USB power. */
        lv_obj_add_flag(w->charge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(w->icon, ok && b->external_power ? LV_SYMBOL_USB : LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(w->icon, UI_COLOR_SUBTEXT, 0);
        lv_label_set_text(w->pct, ok ? "" : "--");
        return;
    }

    if (b->charging) {
        lv_obj_remove_flag(w->charge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(w->charge, LV_OBJ_FLAG_HIDDEN);
    }

    bool low = b->percent >= 0 && b->percent < BATTERY_LOW_PCT && !b->charging;
    lv_label_set_text(w->icon, battery_icon(b));
    lv_obj_set_style_text_color(w->icon, b->charging ? UI_COLOR_OK : (low ? UI_COLOR_LOW : UI_COLOR_TEXT), 0);

    if (b->percent >= 0) {
        lv_label_set_text_fmt(w->pct, "%d%%", b->percent);
    } else {
        lv_label_set_text(w->pct, "--");
    }
}

/**
 * Wi-Fi icon, always shown so its state is readable at a glance:
 *   connected       white, dimmed when the signal is weak
 *   connecting      blinking white
 *   anything else   red (not connected, failed, Wi-Fi off)
 */
static void wifi_apply(const status_widgets_t *w, const gizmo_wifi_status_t *ws)
{
    lv_color_t color = UI_COLOR_LOW;
    lv_opa_t opa = LV_OPA_COVER;

    switch (ws->state) {
    case GIZMO_WIFI_CONNECTED:
        color = UI_COLOR_TEXT;
        opa = ws->rssi >= -75 ? LV_OPA_COVER : LV_OPA_60;
        break;
    case GIZMO_WIFI_CONNECTING:
        color = UI_COLOR_TEXT;
        opa = (s_anim_frame & 1) ? LV_OPA_30 : LV_OPA_COVER;
        break;
    default:
        break;
    }
    lv_obj_set_style_text_color(w->wifi, color, 0);
    lv_obj_set_style_text_opa(w->wifi, opa, 0);
}

/* ---------- clock ---------- */

#define MAX_CLOCKS 2
static lv_obj_t *s_clocks[MAX_CLOCKS];
static int s_clock_count;

static void clock_refresh(void)
{
    char buf[8] = "--:--";
    if (gizmo_core_time_is_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(buf, sizeof(buf), "%H:%M", &tm);
    }
    for (int i = 0; i < s_clock_count; i++) {
        /* Only touch the label when the text changes, to avoid redraws. */
        if (strcmp(lv_label_get_text(s_clocks[i]), buf) != 0) {
            lv_label_set_text(s_clocks[i], buf);
        }
    }
}

void ui_add_clock(lv_obj_t *header)
{
    if (s_clock_count >= MAX_CLOCKS) {
        return;
    }
    lv_obj_t *lbl = lv_label_create(header);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl, "--:--");
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    s_clocks[s_clock_count++] = lbl;
}

static void status_refresh(void)
{
    gizmo_battery_t b;
    bool ok = gizmo_core_get_battery(&b) == ESP_OK;
    gizmo_wifi_status_t ws;
    gizmo_core_wifi_get_status(&ws);

    for (int i = 0; i < s_status_count; i++) {
        battery_apply(&s_status[i], &b, ok);
        wifi_apply(&s_status[i], &ws);
    }
    clock_refresh();
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_anim_frame++;
    status_refresh();   /* reads cached state; no I2C here */
}

void ui_status_start(void)
{
    status_refresh();
    lv_timer_create(status_timer_cb, STATUS_TICK_MS, NULL);
}

/** Right-aligned status icons inside a header. */
static void create_status(lv_obj_t *hdr)
{
    if (s_status_count >= MAX_STATUS) {
        return;
    }
    status_widgets_t *w = &s_status[s_status_count++];

    lv_obj_t *box = lv_obj_create(hdr);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(box, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_column(box, 4, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    /* START, not END: with LV_SIZE_CONTENT, END places children from the right
     * edge of the not-yet-grown box, they overflow to the left, the content
     * size never counts them, and they get clipped. The box itself is already
     * right-aligned in the header. */
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    w->wifi = lv_label_create(box);
    lv_label_set_text(w->wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_pad_right(w->wifi, 6, 0);

    w->charge = lv_label_create(box);
    lv_label_set_text(w->charge, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(w->charge, UI_COLOR_OK, 0);
    lv_obj_add_flag(w->charge, LV_OBJ_FLAG_HIDDEN);

    w->icon = lv_label_create(box);
    lv_obj_set_style_text_font(w->icon, &lv_font_montserrat_20, 0);
    lv_label_set_text(w->icon, LV_SYMBOL_BATTERY_EMPTY);

    w->pct = lv_label_create(box);
    lv_label_set_text(w->pct, "--");
}

/* ---------- header ---------- */

lv_obj_t *ui_create_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_set_size(hdr, LV_PCT(100), UI_HEADER_H);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, UI_COLOR_HEADER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        /* Left-aligned after the back button: a centred title runs into the
         * status icons once it is longer than ~9 characters. */
        lv_obj_t *lbl = lv_label_create(hdr);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 66, 0);
    }

    create_status(hdr);
    return hdr;
}

lv_obj_t *ui_create_back_button(lv_obj_t *header, lv_event_cb_t cb)
{
    lv_obj_t *back = lv_button_create(header);
    lv_obj_set_size(back, 56, UI_HEADER_H - 10);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(back, UI_COLOR_TEXT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return back;
}

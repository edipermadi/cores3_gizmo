/*
 * Home screen: launcher with horizontally swipeable pages.
 *
 *   page 1   Clock, Spectrum, Hopper (apps)
 *   page 2   Settings, Diagnostics (system)
 *
 * A tileview holds one tile per page; dots at the bottom show the active page.
 * Touch: swipe left/right. Encoder: rotate through all icons in page order;
 * focusing an icon on the other page switches to that page (instead of LVGL's
 * scroll-on-focus, which could stop between pages).
 */
#include "ui_priv.h"

#define PAGE_COUNT 2
#define DOTS_H     16

static lv_obj_t *s_tileview;
static lv_obj_t *s_tiles[PAGE_COUNT];
static lv_obj_t *s_dots[PAGE_COUNT];

static void open_settings_cb(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void open_clock_cb(lv_event_t *e)
{
    (void)e;
    ui_show_clock();
}

static void open_game_cb(lv_event_t *e)
{
    (void)e;
    ui_show_game();
}

static void open_spectrum_cb(lv_event_t *e)
{
    (void)e;
    ui_show_spectrum();
}

static void open_diag_cb(lv_event_t *e)
{
    (void)e;
    ui_show_diag();
}

static void update_dots(void)
{
    /* Adding the first icon to the group focuses it (-> icon_focused_cb)
     * before the dots exist; they are refreshed once created. */
    if (!s_dots[PAGE_COUNT - 1]) {
        return;
    }
    lv_obj_t *active = lv_tileview_get_tile_active(s_tileview);
    for (int i = 0; i < PAGE_COUNT; i++) {
        bool on = s_tiles[i] == active;
        lv_obj_set_style_bg_color(s_dots[i], on ? UI_COLOR_TEXT : UI_COLOR_SUBTEXT, 0);
        lv_obj_set_style_bg_opa(s_dots[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

static void page_changed_cb(lv_event_t *e)
{
    (void)e;
    update_dots();
}

/** Encoder moved focus to an icon: bring its page into view. */
static void icon_focused_cb(lv_event_t *e)
{
    lv_obj_t *tile = lv_event_get_user_data(e);
    if (lv_tileview_get_tile_active(s_tileview) != tile) {
        lv_tileview_set_tile(s_tileview, tile, LV_ANIM_ON);
        update_dots();
    }
}

/** Launcher icon: rounded button with a symbol, caption underneath. */
static lv_obj_t *create_app_icon(lv_obj_t *tile, lv_group_t *group, const char *symbol,
                                 const char *name, lv_event_cb_t cb)
{
    lv_obj_t *cell = lv_obj_create(tile);
    lv_obj_set_size(cell, 88, 104);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
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
    /* Page switching is done in icon_focused_cb; don't let focus scroll. */
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, icon_focused_cb, LV_EVENT_FOCUSED, tile);
    ui_add_focusable(group, btn);

    if (symbol) {
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_center(icon);
    }

    lv_obj_t *caption = lv_label_create(cell);
    lv_label_set_text(caption, name);
    lv_obj_set_style_text_color(caption, UI_COLOR_TEXT, 0);

    return btn;
}

/** Little clock face for the Clock icon (the symbol font has no clock). */
static void add_clock_glyph(lv_obj_t *btn)
{
    lv_obj_t *face = lv_obj_create(btn);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, 36, 36);
    lv_obj_center(face);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(face, 3, 0);
    lv_obj_set_style_border_color(face, lv_color_white(), 0);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_CLICKABLE);

    /* Hands at ~10:10, in face coordinates. */
    static const lv_point_precise_t hour[] = { { 18, 18 }, { 11, 12 } };
    static const lv_point_precise_t min[] = { { 18, 18 }, { 27, 8 } };
    const lv_point_precise_t *hands[] = { hour, min };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_line_create(face);
        lv_line_set_points(l, hands[i], 2);
        lv_obj_set_style_line_width(l, 3, 0);
        lv_obj_set_style_line_color(l, lv_color_white(), 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    }
}

/** One launcher page: an icon grid (flex wrap) inside a tileview tile. */
static lv_obj_t *add_page(int col)
{
    lv_obj_t *tile = lv_tileview_add_tile(s_tileview, col, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_pad_all(tile, 16, 0);
    lv_obj_set_style_pad_bottom(tile, DOTS_H, 0);
    lv_obj_set_style_pad_column(tile, 12, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    return tile;
}

lv_obj_t *ui_home_create(lv_group_t *group)
{
    lv_obj_t *scr = ui_create_screen();
    lv_obj_t *hdr = ui_create_header(scr, NULL);
    ui_add_clock(hdr);

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - UI_HEADER_H);
    lv_obj_align(s_tileview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, page_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Page 1: apps */
    s_tiles[0] = add_page(0);
    lv_obj_t *clock_btn = create_app_icon(s_tiles[0], group, NULL, "Clock", open_clock_cb);
    add_clock_glyph(clock_btn);
    create_app_icon(s_tiles[0], group, LV_SYMBOL_AUDIO, "Spectrum", open_spectrum_cb);
    create_app_icon(s_tiles[0], group, LV_SYMBOL_PLAY, "Hopper", open_game_cb);

    /* Page 2: system */
    s_tiles[1] = add_page(1);
    create_app_icon(s_tiles[1], group, LV_SYMBOL_SETTINGS, "Settings", open_settings_cb);
    create_app_icon(s_tiles[1], group, LV_SYMBOL_EYE_OPEN, "Diagnostics", open_diag_cb);

    /* Page indicator dots, on top of the tileview. */
    lv_obj_t *dots = lv_obj_create(scr);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, DOTS_H);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 8, 0);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < PAGE_COUNT; i++) {
        s_dots[i] = lv_obj_create(dots);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 8, 8);
        lv_obj_set_style_radius(s_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
    update_dots();
    return scr;
}

#pragma once

/**
 * @file
 * @brief Shared helpers between gizmo_ui source files (private).
 *
 * File map:
 *  - gizmo_ui.c    entry point, navigation, encoder group handling
 *  - ui_common.c   theme, screens, top bar (battery + Wi-Fi status)
 *  - ui_home.c     home launcher
 *  - ui_settings.c settings screen with tabs
 *  - ui_display.c  "Display" tab (brightness)
 *  - ui_network.c  "Network" tab (Wi-Fi scan/connect, password entry, timezone)
 *  - ui_diag.c     "Diagnostics" screen: I2C scan and PMU tabs
 *  - ui_spectrum.c "Spectrum" screen: live microphone spectrum
 *  - ui_clock.c    "Clock" screen: analog / digital, 24h / 12h
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "gizmo_core.h"
#include "lvgl.h"

/** Colour palette (0xRRGGBB); dark or light, chosen at startup. */
typedef struct {
    uint32_t bg, header, accent, text, subtext, card, ok, low;
    bool dark;
} ui_palette_t;

/** Active palette, set by ui_theme_init() from the saved dark-mode preference. */
extern const ui_palette_t *ui_pal;

#define UI_COLOR_BG        lv_color_hex(ui_pal->bg)
#define UI_COLOR_HEADER    lv_color_hex(ui_pal->header)
#define UI_COLOR_ACCENT    lv_color_hex(ui_pal->accent)
#define UI_COLOR_TEXT      lv_color_hex(ui_pal->text)
#define UI_COLOR_SUBTEXT   lv_color_hex(ui_pal->subtext)
#define UI_COLOR_CARD      lv_color_hex(ui_pal->card)
#define UI_COLOR_OK        lv_color_hex(ui_pal->ok)
#define UI_COLOR_LOW       lv_color_hex(ui_pal->low)

#define UI_HEADER_H        40
#define UI_ANIM_MS         200

/* ---- ui_common.c ---- */

/** Pick the palette (saved dark/light preference), LVGL base theme and focus style. */
void ui_theme_init(void);

/** True when the dark palette is active. */
bool ui_dark_mode(void);

/**
 * Save the dark/light preference and restart the device so every screen is
 * rebuilt with the new palette (colours are applied when widgets are created).
 */
void ui_set_dark_mode_and_restart(bool dark);

/** Add obj to an encoder group with the white focus outline. */
void ui_add_focusable(lv_group_t *group, lv_obj_t *obj);

/** Plain dark full-screen object. */
lv_obj_t *ui_create_screen(void);

/** Home top bar: optional title on the left and status icons on the right. */
lv_obj_t *ui_create_header(lv_obj_t *parent, const char *title);


/** True for 24-hour time display (saved preference, default on). */
bool ui_clock_24h(void);

/** Switch 24h/12h display and save it. */
void ui_clock_set_24h(bool on);

/** "13:45" (24h) or "1:45" / "1:45 PM" (12h, `with_ampm`), per ui_clock_24h(). */
void ui_format_hm(char *buf, size_t len, const struct tm *tm, bool with_ampm);

/** Time clock at the left of a header, 24h or 12h ("--:--" until time is known). */
void ui_add_clock(lv_obj_t *header);

/** Start the periodic status bar refresh (battery, Wi-Fi, clock). */
void ui_status_start(void);

/* ---- gizmo_ui.c ---- */

void ui_show_home(void);
void ui_show_settings(void);
void ui_show_diag(void);
void ui_show_spectrum(void);
void ui_show_clock(void);

/**
 * Pages other than home are full screen with no back button: HMI button 1
 * (ESC, via the widgets in the page's group) goes back, and so does a swipe
 * right anywhere on `screen` (touch). Widgets that are dragged
 * horizontally (sliders) must clear LV_OBJ_FLAG_GESTURE_BUBBLE.
 */
void ui_enable_back_gesture(lv_obj_t *screen);

/** Point the encoder (if present) at group. */
void ui_use_group(lv_group_t *group);

/**
 * Replace group contents with the focusable widgets under each root,
 * in tree order, keeping the current focus if it is still present.
 */
void ui_group_rebuild(lv_group_t *group, lv_obj_t *const *roots, int n_roots);

/* ---- screens and tabs ---- */

lv_obj_t *ui_home_create(lv_group_t *group);

lv_obj_t *ui_settings_create(void);
lv_group_t *ui_settings_group(void);
/** Re-collect focusable widgets; call after a tab adds/removes widgets. */
void ui_settings_refresh_group(void);
/** Called each time the settings screen is shown. */
void ui_settings_on_show(void);

void ui_display_tab_create(lv_obj_t *page);

lv_obj_t *ui_clock_create(void);
lv_group_t *ui_clock_group(void);
/** Start / stop the clock page's tick with the screen. */
void ui_clock_on_show(void);
void ui_clock_on_hide(void);

lv_obj_t *ui_spectrum_create(void);
lv_group_t *ui_spectrum_group(void);
/** Start / stop the analyser with the screen. */
void ui_spectrum_on_show(void);
void ui_spectrum_on_hide(void);

lv_obj_t *ui_diag_create(void);
lv_group_t *ui_diag_group(void);
/** Called each time the Diagnostics screen is shown. */
void ui_diag_on_show(void);
void ui_network_tab_create(lv_obj_t *page);
/** Called when the Network tab becomes visible. */
void ui_network_tab_shown(void);

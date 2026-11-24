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
 */

#include "gizmo_core.h"
#include "lvgl.h"

#define UI_COLOR_BG        lv_color_hex(0x101418)
#define UI_COLOR_HEADER    lv_color_hex(0x1c232b)
#define UI_COLOR_ACCENT    lv_color_hex(0x2f80ed)
#define UI_COLOR_TEXT      lv_color_hex(0xe8edf2)
#define UI_COLOR_SUBTEXT   lv_color_hex(0x8a97a6)
#define UI_COLOR_CARD      lv_color_hex(0x1c232b)
#define UI_COLOR_OK        lv_color_hex(0x4cd964)
#define UI_COLOR_LOW       lv_color_hex(0xff5f57)

#define UI_HEADER_H        40
#define UI_ANIM_MS         200

/* ---- ui_common.c ---- */

/** Dark theme and the shared focus-outline style. */
void ui_theme_init(void);

/** Add obj to an encoder group with the white focus outline. */
void ui_add_focusable(lv_group_t *group, lv_obj_t *obj);

/** Plain dark full-screen object. */
lv_obj_t *ui_create_screen(void);

/** Top bar with optional title (left, after the back button) and status icons on the right. */
lv_obj_t *ui_create_header(lv_obj_t *parent, const char *title);

/** Transparent "<" button at the left of a header. */
lv_obj_t *ui_create_back_button(lv_obj_t *header, lv_event_cb_t cb);

/** 24h "HH:MM" clock at the left of a header ("--:--" until time is known from RTC or NTP). */
void ui_add_clock(lv_obj_t *header);

/** Start the periodic status bar refresh (battery, Wi-Fi, clock). */
void ui_status_start(void);

/* ---- gizmo_ui.c ---- */

void ui_show_home(void);
void ui_show_settings(void);
void ui_show_diag(void);
void ui_show_spectrum(void);

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

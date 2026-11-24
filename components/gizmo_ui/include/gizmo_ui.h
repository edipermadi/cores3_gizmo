#pragma once

/**
 * @file
 * @brief LVGL screens: home launcher and settings.
 *
 * Pure LVGL; all hardware access goes through gizmo_core.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the home and settings screens and show the home screen.
 *
 * Requires gizmo_core_init() to have run, and must be called with the LVGL
 * lock held (gizmo_core_lock()).
 */
void gizmo_ui_init(void);

#ifdef __cplusplus
}
#endif

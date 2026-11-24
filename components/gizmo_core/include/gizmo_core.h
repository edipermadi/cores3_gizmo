#pragma once

/**
 * @file
 * @brief Public API of the board layer.
 *
 * Owns everything hardware-specific: display and touch bring-up, backlight
 * brightness (persisted in NVS), the LVGL lock, and the optional M5Stack
 * Module HMI encoder. Other components never include the BSP directly.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GIZMO_BRIGHTNESS_MIN 10
#define GIZMO_BRIGHTNESS_MAX 100

/**
 * @brief Bring up the board: settings storage, display, touch and LVGL task.
 *
 * Applies the stored brightness to the backlight.
 */
esp_err_t gizmo_core_init(void);

/** Current brightness percent (GIZMO_BRIGHTNESS_MIN..GIZMO_BRIGHTNESS_MAX). */
int gizmo_core_get_brightness(void);

/**
 * @brief Set the backlight brightness.
 *
 * @param percent Brightness percent, clamped to the valid range.
 * @param persist true to also save it so it is restored on boot.
 */
esp_err_t gizmo_core_set_brightness(int percent, bool persist);

/**
 * @brief LVGL encoder input from the Module HMI.
 *
 * Rotate = next/prev (or change value when editing), encoder push = enter,
 * button 1 = escape (back), button 2 = enter.
 *
 * @return The input device, or NULL if the module is not attached.
 */
lv_indev_t *gizmo_core_get_encoder(void);

/** Take the LVGL lock. timeout_ms 0 blocks forever. */
bool gizmo_core_lock(uint32_t timeout_ms);

/** Release the LVGL lock. */
void gizmo_core_unlock(void);

#ifdef __cplusplus
}
#endif

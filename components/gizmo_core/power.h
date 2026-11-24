#pragma once

/**
 * @file
 * @brief AXP2101 battery status and charge control (private to gizmo_core).
 *
 * A background task polls the PMIC every second, enables battery charging only
 * while external (USB) power is present, and caches the battery state. It also
 * watches the power key (left side button): a short press powers off.
 */

#include "esp_err.h"
#include "gizmo_core.h"

/** Attach to the AXP2101, enable its fuel gauge and start the poll task. */
esp_err_t power_init(void);

/** Latest cached battery state. ESP_ERR_INVALID_STATE if the last poll failed. */
esp_err_t power_get_battery(gizmo_battery_t *out);

/** Turn the backlight off and cut power via the PMIC. */
void power_off(void);

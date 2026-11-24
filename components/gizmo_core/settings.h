#pragma once

/**
 * @file
 * @brief Persistent settings in NVS (private to gizmo_core).
 */

#include "esp_err.h"
#include "gizmo_core.h"

#define SETTINGS_BRIGHTNESS_DEFAULT 80

/** Initialise NVS storage used for persistent settings. */
esp_err_t settings_init(void);

/** Load stored brightness percent, or the default if none is stored. */
int settings_get_brightness(void);

/** Persist brightness percent to NVS. */
esp_err_t settings_set_brightness(int percent);

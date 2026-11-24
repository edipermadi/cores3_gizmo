#pragma once

/**
 * @file
 * @brief Persistent settings in NVS (private to gizmo_core).
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gizmo_core.h"

#define SETTINGS_BRIGHTNESS_DEFAULT 80

/** Initialise NVS storage used for persistent settings. */
esp_err_t settings_init(void);

/** Load stored brightness percent, or the default if none is stored. */
int settings_get_brightness(void);

/** Persist brightness percent to NVS. */
esp_err_t settings_set_brightness(int percent);

/** Load the stored IANA timezone name into buf. ESP_ERR_NOT_FOUND if none stored. */
esp_err_t settings_get_timezone(char *buf, size_t len);

/** Persist the IANA timezone name. */
esp_err_t settings_set_timezone(const char *name);

/** Generic small preference (NVS key max 15 chars); returns def if unset. */
uint8_t settings_get_u8(const char *key, uint8_t def);

/** Persist a generic small preference. */
esp_err_t settings_set_u8(const char *key, uint8_t value);

/** Generic 32-bit preference; returns def if unset. */
uint32_t settings_get_u32(const char *key, uint32_t def);

/** Persist a generic 32-bit preference. */
esp_err_t settings_set_u32(const char *key, uint32_t value);

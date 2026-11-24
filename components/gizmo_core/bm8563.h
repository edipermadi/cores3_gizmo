#pragma once

/**
 * @file
 * @brief BM8563 real-time clock (private to gizmo_core).
 *
 * Battery-backed clock on the CoreS3 internal I2C bus (0x51, PCF8563
 * compatible). Holds UTC so it is independent of the configured timezone.
 */

#include <time.h>

#include "esp_err.h"

/** Attach to the RTC and make sure its oscillator is running. */
esp_err_t bm8563_init(void);

/**
 * @brief Read UTC time from the RTC.
 *
 * @return ESP_ERR_INVALID_STATE if the RTC lost power (time not trustworthy).
 */
esp_err_t bm8563_read(time_t *out);

/** Write UTC time to the RTC (also clears the low-voltage flag). */
esp_err_t bm8563_write(time_t t);

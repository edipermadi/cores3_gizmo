#pragma once

/**
 * @file
 * @brief Bottom3 base RGB LEDs (private to gizmo_core).
 *
 * 10 x WS2812 on the M5GO Bottom3 base, 5 per side, one data line
 * (M-Bus pin 8 = GPIO5 on CoreS3 per the Bottom3 pin map; verified).
 * Chain order (verified): down the right bar 0 (top) .. 4 (bottom), then up
 * the left bar 5 (bottom) .. 9 (top). Pin and count are Kconfig options.
 */

#include <stdint.h>

#include "esp_err.h"

/** Create the RMT strip and switch all LEDs off. */
esp_err_t leds_init(void);

/** Number of LEDs (0 if the strip is unavailable). */
int leds_count(void);

/** Set one LED (buffered until leds_show()). Out-of-range indices are ignored. */
void leds_set(int index, uint8_t r, uint8_t g, uint8_t b);

/** Switch all LEDs off (buffered). */
void leds_clear(void);

/** Send the buffered colours to the strip. */
esp_err_t leds_show(void);

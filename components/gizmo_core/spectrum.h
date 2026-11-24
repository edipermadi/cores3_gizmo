#pragma once

/**
 * @file
 * @brief Microphone spectrum analyser (private to gizmo_core).
 */

#include "esp_err.h"
#include "gizmo_core.h"

/** Open the microphones and start the capture/FFT task. */
esp_err_t spectrum_start(void);

/** Stop capturing and close the microphones. */
void spectrum_stop(void);

/** Latest spectrum. ESP_ERR_INVALID_STATE until the first frame is ready. */
esp_err_t spectrum_get(gizmo_spectrum_t *out);

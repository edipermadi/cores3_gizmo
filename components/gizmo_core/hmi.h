#pragma once

/**
 * @file
 * @brief M5Stack Module HMI driver (private to gizmo_core).
 *
 * Polls the module over the CoreS3 internal I2C bus and exposes it as an
 * LVGL encoder input device.
 */

#include "esp_err.h"
#include "lvgl.h"

/** Probe the module and register it as an LVGL encoder. ESP_ERR_NOT_FOUND if absent. */
esp_err_t hmi_init(void);

/** LVGL encoder input device, or NULL if the module was not found. */
lv_indev_t *hmi_get_indev(void);

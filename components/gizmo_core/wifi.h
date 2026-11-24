#pragma once

/**
 * @file
 * @brief Wi-Fi station management (private to gizmo_core).
 *
 * Credentials are stored by the Wi-Fi driver in NVS, so the last network is
 * reconnected automatically at boot.
 */

#include "esp_err.h"
#include "gizmo_core.h"

/** Start Wi-Fi in station mode and reconnect to the saved network, if any. */
esp_err_t wifi_init(void);

esp_err_t wifi_scan_start(void);
int wifi_get_scan_results(gizmo_wifi_ap_t *out, int max);
esp_err_t wifi_connect(const char *ssid, const char *password);
esp_err_t wifi_forget(void);
void wifi_get_status(gizmo_wifi_status_t *out);

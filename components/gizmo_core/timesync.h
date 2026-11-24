#pragma once

/**
 * @file
 * @brief System time from the BM8563 RTC and NTP (private to gizmo_core).
 *
 * At boot the clock is restored from the RTC if it holds a valid time. SNTP
 * starts the first time Wi-Fi gets an IP address (lwIP then re-syncs
 * periodically) and every NTP sync is written back to the RTC.
 */

#include <stdbool.h>

#include "esp_err.h"

/** Apply the stored timezone (or CONFIG_GIZMO_DEFAULT_TIMEZONE) and restore time from the RTC. */
void timesync_init(void);

/** Start SNTP if not already running. Call when the station gets an IP. */
void timesync_on_connected(void);

/** True once the clock holds a real time (from the RTC or NTP). */
bool timesync_is_valid(void);

/** True once NTP has synced at least once this boot. */
bool timesync_is_ntp_synced(void);

/** Index of the active timezone in the built-in table. */
int timesync_tz_get(void);

/** Switch timezone (takes effect immediately) and persist it. */
esp_err_t timesync_tz_set(int idx);

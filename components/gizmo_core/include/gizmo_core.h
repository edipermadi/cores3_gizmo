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

/** Battery state from the power management chip. */
typedef struct {
    bool present;        /**< A battery is connected. */
    bool charging;       /**< Battery is currently charging (enabled only on USB power). */
    bool external_power; /**< USB / bus 5 V is present. */
    int percent;         /**< State of charge 0..100, or -1 if unknown. */
} gizmo_battery_t;

/** Maximum networks kept from one Wi-Fi scan. */
#define GIZMO_WIFI_MAX_APS 16

/** Wi-Fi station state. */
typedef enum {
    GIZMO_WIFI_OFF,          /**< Wi-Fi not initialised. */
    GIZMO_WIFI_DISCONNECTED, /**< Idle, no network configured or user disconnected. */
    GIZMO_WIFI_CONNECTING,   /**< Associating / waiting for IP / retrying. */
    GIZMO_WIFI_CONNECTED,    /**< Associated and has an IP address. */
    GIZMO_WIFI_FAILED,       /**< Gave up; see fail_reason. */
} gizmo_wifi_state_t;

/** Snapshot of the Wi-Fi station. */
typedef struct {
    gizmo_wifi_state_t state;
    char ssid[33];             /**< Network being used or last attempted. */
    char ip[16];               /**< Dotted IPv4 address when connected. */
    int rssi;                  /**< Signal strength in dBm when connected. */
    const char *fail_reason;   /**< Human-readable reason when FAILED, else NULL. */
    bool scanning;             /**< A scan is in progress. */
    uint32_t scan_generation;  /**< Increments each time new scan results are ready. */
} gizmo_wifi_status_t;

/** One network from a scan. */
typedef struct {
    char ssid[33];
    int rssi;      /**< dBm */
    bool secure;   /**< Needs a password. */
} gizmo_wifi_ap_t;

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
 * @brief Get the latest battery state.
 *
 * Returns a value cached by a background task that polls the PMIC every
 * second (and keeps charging enabled only while USB power is present), so it
 * is cheap and safe to call from LVGL timers.
 */
esp_err_t gizmo_core_get_battery(gizmo_battery_t *out);

/**
 * @brief Start an asynchronous Wi-Fi scan.
 *
 * Poll gizmo_core_wifi_get_status(); when scan_generation changes, fetch the
 * results with gizmo_core_wifi_get_scan_results().
 */
esp_err_t gizmo_core_wifi_scan(void);

/** Copy up to max networks from the last scan, strongest first. Returns the count. */
int gizmo_core_wifi_get_scan_results(gizmo_wifi_ap_t *out, int max);

/**
 * @brief Connect to a network and remember it for the next boot.
 *
 * @param ssid     Network name.
 * @param password NULL or "" for open networks.
 */
esp_err_t gizmo_core_wifi_connect(const char *ssid, const char *password);

/** Disconnect and forget the saved network. */
esp_err_t gizmo_core_wifi_forget(void);

/** Current Wi-Fi state. Non-blocking; safe to call from LVGL timers. */
void gizmo_core_wifi_get_status(gizmo_wifi_status_t *out);

/**
 * @brief Whether the system clock holds a real time.
 *
 * True after the time is restored from the battery-backed BM8563 RTC at boot,
 * or after the first NTP sync once Wi-Fi connects. Read the time with time() /
 * localtime_r(), which use the timezone chosen with gizmo_core_tz_set().
 */
bool gizmo_core_time_is_valid(void);

/** Whether NTP has synced at least once since boot (RTC time is then refreshed). */
bool gizmo_core_time_is_ntp_synced(void);

/** Number of built-in timezones (ordered west to east by UTC offset). */
int gizmo_core_tz_count(void);

/** IANA name of timezone index, e.g. "Asia/Singapore"; NULL if out of range. */
const char *gizmo_core_tz_name(int index);

/** Standard (non-DST) UTC offset of timezone index, in minutes. */
int gizmo_core_tz_offset_min(int index);

/** Index of the active timezone. */
int gizmo_core_tz_get(void);

/**
 * @brief Switch timezone.
 *
 * Takes effect immediately for localtime_r() and is saved to NVS for the next
 * boot. The RTC keeps UTC, so it is unaffected.
 */
esp_err_t gizmo_core_tz_set(int index);

/**
 * @brief Switch the device off (backlight off, then PMIC power-off).
 *
 * Also triggered automatically by a short press of the power key on the left
 * side. Does not return when running on battery.
 */
void gizmo_core_power_off(void);

/**
 * @brief Probe every 7-bit address (0x08-0x77) on the internal I2C bus.
 *
 * Blocks for roughly 100-300 ms. Call from the UI on demand, not every frame.
 *
 * @param addrs Receives the responding addresses in ascending order.
 * @param max   Capacity of addrs.
 * @return Number of devices found, or -1 if the bus is not available.
 */
int gizmo_core_i2c_scan(uint8_t *addrs, int max);

/** Known device name for an internal-bus address, or NULL if unknown. */
const char *gizmo_core_i2c_device_name(uint8_t addr);

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

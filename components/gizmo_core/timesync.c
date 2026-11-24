/*
 * System time: BM8563 RTC at boot, NTP once online.
 *
 *   boot        RTC valid?  -> settimeofday() from RTC, time usable offline
 *   Wi-Fi up    start SNTP  -> on each sync, write the result back to the RTC
 *
 * NTP is authoritative; the RTC only bridges reboots and offline use.
 */
#include "timesync.h"

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "bm8563.h"
#include "settings.h"
#include "timezones.h"
#include "sdkconfig.h"

static const char *TAG = "timesync";

static bool s_started;
static bool s_have_rtc;
static volatile bool s_valid;
static volatile bool s_ntp_synced;
static int s_tz = -1;

static void apply_tz(int idx)
{
    s_tz = idx;
    setenv("TZ", g_timezones[idx].posix, 1);
    tzset();
}

static void log_time(const char *what, time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "%s: %s (%s)", what, buf, g_timezones[s_tz].name);
}

static void sync_cb(struct timeval *tv)
{
    s_ntp_synced = true;
    s_valid = true;
    log_time("Time synced from NTP", tv->tv_sec);

    if (s_have_rtc) {
        esp_err_t err = bm8563_write(tv->tv_sec);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update RTC: %s", esp_err_to_name(err));
        }
    }
}

void timesync_init(void)
{
    /* Stored choice, else the Kconfig default, else UTC. */
    char name[48];
    int idx = -1;
    if (settings_get_timezone(name, sizeof(name)) == ESP_OK) {
        idx = timezone_find(name);
    }
    if (idx < 0) {
        idx = timezone_find(CONFIG_GIZMO_DEFAULT_TIMEZONE);
    }
    if (idx < 0) {
        ESP_LOGW(TAG, "Unknown timezone \"%s\", using Etc/UTC", CONFIG_GIZMO_DEFAULT_TIMEZONE);
        idx = timezone_find("Etc/UTC");
    }
    apply_tz(idx);
    ESP_LOGI(TAG, "Timezone %s (%s)", g_timezones[idx].name, g_timezones[idx].posix);

    esp_err_t err = bm8563_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC unavailable: %s", esp_err_to_name(err));
        return;
    }
    s_have_rtc = true;

    time_t t;
    if (bm8563_read(&t) == ESP_OK) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        s_valid = true;
        log_time("Time restored from RTC", t);
    } else {
        ESP_LOGI(TAG, "RTC time not valid; waiting for NTP");
    }
}

void timesync_on_connected(void)
{
    if (s_started) {
        return;   /* lwIP keeps re-syncing (CONFIG_LWIP_SNTP_UPDATE_DELAY) */
    }

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_GIZMO_NTP_SERVER);
    cfg.sync_cb = sync_cb;
    cfg.wait_for_sync = false;   /* nobody blocks on it; we use the callback */

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        return;
    }
    s_started = true;
    ESP_LOGI(TAG, "SNTP started (%s)", CONFIG_GIZMO_NTP_SERVER);
}

bool timesync_is_valid(void)
{
    return s_valid;
}

bool timesync_is_ntp_synced(void)
{
    return s_ntp_synced;
}

int timesync_tz_get(void)
{
    return s_tz;
}

esp_err_t timesync_tz_set(int idx)
{
    if (idx < 0 || idx >= g_timezone_count) {
        return ESP_ERR_INVALID_ARG;
    }
    apply_tz(idx);
    ESP_LOGI(TAG, "Timezone set to %s", g_timezones[idx].name);
    return settings_set_timezone(g_timezones[idx].name);
}

#include "gizmo_core.h"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hmi.h"
#include "leds.h"
#include "power.h"
#include "settings.h"
#include "spectrum.h"
#include "timesync.h"
#include "timezones.h"
#include "wifi.h"

static const char *TAG = "gizmo_core";

static int s_brightness = SETTINGS_BRIGHTNESS_DEFAULT;

static int clamp_brightness(int percent)
{
    if (percent < GIZMO_BRIGHTNESS_MIN) {
        return GIZMO_BRIGHTNESS_MIN;
    }
    if (percent > GIZMO_BRIGHTNESS_MAX) {
        return GIZMO_BRIGHTNESS_MAX;
    }
    return percent;
}

esp_err_t gizmo_core_init(void)
{
    ESP_RETURN_ON_ERROR(settings_init(), TAG, "settings init failed");

    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(gizmo_core_set_brightness(settings_get_brightness(), false),
                        TAG, "brightness init failed");

    /* Battery status is informational: keep running if it can't be read. */
    esp_err_t err = power_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Battery status unavailable: %s", esp_err_to_name(err));
    }

    timesync_init();

    /* Base LEDs (Bottom3) are optional too. */
    err = leds_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Base LEDs unavailable: %s", esp_err_to_name(err));
    }

    /* Network is optional for the UI: log and carry on. */
    err = wifi_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
    }

    /* Optional hardware: carry on with touch only if the module is absent. */
    err = hmi_init();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Module HMI init failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t gizmo_core_get_pmu_info(gizmo_pmu_info_t *out)
{
    return power_get_info(out);
}

esp_err_t gizmo_core_get_battery(gizmo_battery_t *out)
{
    return power_get_battery(out);
}

esp_err_t gizmo_core_wifi_scan(void)
{
    return wifi_scan_start();
}

int gizmo_core_wifi_get_scan_results(gizmo_wifi_ap_t *out, int max)
{
    return wifi_get_scan_results(out, max);
}

esp_err_t gizmo_core_wifi_connect(const char *ssid, const char *password)
{
    return wifi_connect(ssid, password);
}

esp_err_t gizmo_core_wifi_forget(void)
{
    return wifi_forget();
}

void gizmo_core_wifi_get_status(gizmo_wifi_status_t *out)
{
    wifi_get_status(out);
}

bool gizmo_core_time_is_valid(void)
{
    return timesync_is_valid();
}

bool gizmo_core_time_is_ntp_synced(void)
{
    return timesync_is_ntp_synced();
}

int gizmo_core_tz_count(void)
{
    return g_timezone_count;
}

const char *gizmo_core_tz_name(int index)
{
    return (index >= 0 && index < g_timezone_count) ? g_timezones[index].name : NULL;
}

int gizmo_core_tz_offset_min(int index)
{
    return (index >= 0 && index < g_timezone_count) ? g_timezones[index].std_offset_min : 0;
}

int gizmo_core_tz_get(void)
{
    return timesync_tz_get();
}

esp_err_t gizmo_core_tz_set(int index)
{
    return timesync_tz_set(index);
}

void gizmo_core_restart(void)
{
    ESP_LOGI(TAG, "Restarting");
    /* Let the caller's last frame (e.g. the switch animation) reach the panel. */
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

void gizmo_core_power_off(void)
{
    power_off();
}

/* Devices known to live on the CoreS3 internal I2C bus (plus Module HMI). */
static const struct {
    uint8_t addr;
    const char *name;
} s_known_i2c[] = {
    { 0x10, "BMM150 magnetometer" },
    { 0x21, "GC0308 camera" },
    { 0x23, "LTR-553 light/proximity" },
    { 0x34, "AXP2101 power" },
    { 0x36, "AW88298 speaker amp" },
    { 0x38, "FT6336 touch" },
    { 0x40, "ES7210 mic codec" },
    { 0x41, "Module HMI" },
    { 0x51, "BM8563 RTC" },
    { 0x58, "AW9523 GPIO expander" },
    { 0x68, "BMI270 IMU" },
    { 0x69, "BMI270 IMU" },
};

const char *gizmo_core_i2c_device_name(uint8_t addr)
{
    for (size_t i = 0; i < sizeof(s_known_i2c) / sizeof(s_known_i2c[0]); i++) {
        if (s_known_i2c[i].addr == addr) {
            return s_known_i2c[i].name;
        }
    }
    return NULL;
}

static int i2c_scan_here(uint8_t *addrs, int max)
{
    i2c_master_bus_handle_t bus;
    if (i2c_master_get_bus_handle(BSP_I2C_NUM, &bus) != ESP_OK) {
        return -1;
    }
    int n = 0;
    /* 7-bit addresses outside the reserved ranges. */
    for (uint8_t a = 0x08; a <= 0x77 && n < max; a++) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) {
            addrs[n++] = a;
        }
    }
    return n;
}

typedef struct {
    uint8_t *addrs;
    int max;
    int n;
    SemaphoreHandle_t done;
} scan_job_t;

static void scan_worker(void *arg)
{
    scan_job_t *job = arg;
    job->n = i2c_scan_here(job->addrs, job->max);
    xSemaphoreGive(job->done);
    vTaskDelete(NULL);
}

/*
 * i2c_master_probe() must run on the core that owns the I2C interrupt (core 0,
 * where the BSP created the bus). Called from core 1 (e.g. the LVGL task) it
 * loses a race with the ISR and reports every other address as present
 * (measured: 9 devices from core 0, 53-55 from core 1). Normal device
 * transfers are not affected, only the probe's NACK detection.
 */
#define I2C_BUS_CORE 0

int gizmo_core_i2c_scan(uint8_t *addrs, int max)
{
    int n;
    if (xPortGetCoreID() == I2C_BUS_CORE) {
        n = i2c_scan_here(addrs, max);
    } else {
        scan_job_t job = { .addrs = addrs, .max = max, .n = -1,
                           .done = xSemaphoreCreateBinary() };
        if (!job.done) {
            return -1;
        }
        if (xTaskCreatePinnedToCore(scan_worker, "i2c_scan", 3072, &job, 5, NULL,
                                    I2C_BUS_CORE) != pdPASS) {
            vSemaphoreDelete(job.done);
            return -1;
        }
        xSemaphoreTake(job.done, portMAX_DELAY);
        vSemaphoreDelete(job.done);
        n = job.n;
    }
    ESP_LOGI(TAG, "I2C scan: %d device(s)", n);
    return n;
}

esp_err_t gizmo_core_spectrum_start(void)
{
    return spectrum_start();
}

void gizmo_core_spectrum_stop(void)
{
    spectrum_stop();
}

esp_err_t gizmo_core_spectrum_get(gizmo_spectrum_t *out)
{
    return spectrum_get(out);
}

int gizmo_core_leds_count(void)
{
    return leds_count();
}

esp_err_t gizmo_core_leds_set(int index, uint8_t r, uint8_t g, uint8_t b)
{
    leds_set(index, r, g, b);
    return leds_show();
}

esp_err_t gizmo_core_leds_off(void)
{
    leds_clear();
    return leds_show();
}

uint8_t gizmo_core_pref_get_u8(const char *key, uint8_t def)
{
    return settings_get_u8(key, def);
}

esp_err_t gizmo_core_pref_set_u8(const char *key, uint8_t value)
{
    return settings_set_u8(key, value);
}

uint32_t gizmo_core_pref_get_u32(const char *key, uint32_t def)
{
    return settings_get_u32(key, def);
}

esp_err_t gizmo_core_pref_set_u32(const char *key, uint32_t value)
{
    return settings_set_u32(key, value);
}

lv_indev_t *gizmo_core_get_encoder(void)
{
    return hmi_get_indev();
}

int gizmo_core_get_brightness(void)
{
    return s_brightness;
}

esp_err_t gizmo_core_set_brightness(int percent, bool persist)
{
    s_brightness = clamp_brightness(percent);
    ESP_RETURN_ON_ERROR(bsp_display_brightness_set(s_brightness), TAG, "set brightness failed");

    if (persist) {
        ESP_RETURN_ON_ERROR(settings_set_brightness(s_brightness), TAG, "save brightness failed");
    }
    return ESP_OK;
}

bool gizmo_core_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void gizmo_core_unlock(void)
{
    bsp_display_unlock();
}

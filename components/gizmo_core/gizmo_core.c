#include "gizmo_core.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "hmi.h"
#include "power.h"
#include "settings.h"
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

/**
 * Check the internal I2C bus is idle before the BSP uses it.
 *
 * After a USB (esptool) reset the ESP32-S3 restarts but bus devices do not,
 * and one left mid-transfer can hold SCL low. The BSP then fails its first
 * PMIC write with ESP_ERROR_CHECK and the board boot-loops on a white screen.
 * Try the standard 9-clock recovery; if SCL is still held, report it clearly.
 */
static esp_err_t check_i2c_bus(void)
{
    const gpio_num_t sda = BSP_I2C_SDA, scl = BSP_I2C_SCL;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(50);

    if (gpio_get_level(scl) && gpio_get_level(sda)) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "I2C bus busy (SDA=%d SCL=%d), attempting recovery",
             gpio_get_level(sda), gpio_get_level(scl));
    for (int i = 0; i < 9 && !gpio_get_level(sda); i++) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
    }
    /* STOP condition */
    gpio_set_level(sda, 0);
    esp_rom_delay_us(5);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(5);

    bool ok = gpio_get_level(scl) && gpio_get_level(sda);
    gpio_reset_pin(sda);
    gpio_reset_pin(scl);
    if (!ok) {
        ESP_LOGE(TAG, "I2C SCL is held low by a device on the bus. "
                      "Press the RST button (a USB/esptool reset does not reset bus devices).");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "I2C bus recovered");
    return ESP_OK;
}

esp_err_t gizmo_core_init(void)
{
    ESP_RETURN_ON_ERROR(check_i2c_bus(), TAG, "I2C bus stuck");
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

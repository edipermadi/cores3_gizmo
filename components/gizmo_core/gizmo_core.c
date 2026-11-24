#include "gizmo_core.h"

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "hmi.h"
#include "settings.h"

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

    /* Optional hardware: carry on with touch only if the module is absent. */
    esp_err_t err = hmi_init();
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Module HMI init failed: %s", esp_err_to_name(err));
    }
    return ESP_OK;
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

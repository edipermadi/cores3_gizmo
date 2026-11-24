#include "leds.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "leds";

static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_mutex;   /* console and spectrum task may both write */

esp_err_t leds_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "mutex");

    const led_strip_config_t cfg = {
        .strip_gpio_num = CONFIG_GIZMO_LED_GPIO,
        .max_leds = CONFIG_GIZMO_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&cfg, &rmt_cfg, &s_strip), TAG, "strip init");
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "%d LEDs on GPIO%d", CONFIG_GIZMO_LED_COUNT, CONFIG_GIZMO_LED_GPIO);
    return ESP_OK;
}

int leds_count(void)
{
    return s_strip ? CONFIG_GIZMO_LED_COUNT : 0;
}

void leds_set(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip || index < 0 || index >= CONFIG_GIZMO_LED_COUNT) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    led_strip_set_pixel(s_strip, index, r, g, b);
    xSemaphoreGive(s_mutex);
}

void leds_clear(void)
{
    for (int i = 0; i < leds_count(); i++) {
        leds_set(i, 0, 0, 0);
    }
}

esp_err_t leds_show(void)
{
    if (!s_strip) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    esp_err_t err = led_strip_refresh(s_strip);
    xSemaphoreGive(s_mutex);
    return err;
}

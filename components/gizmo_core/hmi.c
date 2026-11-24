/*
 * M5Stack Module HMI (encoder + 3 buttons, STM32 co-processor at I2C 0x41).
 *
 * The module sits on the CoreS3 internal I2C bus (M5-Bus 17/18 = GPIO12/11),
 * which the BSP already owns, so we attach to that bus instead of creating one.
 *
 * Registers (Module HMI I2C protocol V1):
 *   0x10  int32 LE  encoder increment, cleared on read
 *   0x20  u8        button S (encoder push), 0/1
 *   0x21  u8        button 1, 0/1
 *   0x22  u8        button 2, 0/1
 *
 * Read registers with exactly the length the module expects, one register
 * group per transfer (as M5Stack's library does). A 3-byte read across
 * 0x20..0x22 makes the module's firmware stretch SCL forever, hanging the
 * whole internal bus (PMIC, RTC, touch) until the RST button resets it.
 *   0x30  u8 x2     LED 1, LED 2 (1 = on)
 *   0x40  u8        write 1 to reset the accumulated counter
 */
#include "hmi.h"

#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "hmi";

#define HMI_ADDR            0x41
#define HMI_I2C_SPEED_HZ    100000
#define HMI_TIMEOUT_MS      20
#define HMI_PROBE_TRIES     15
#define HMI_PROBE_DELAY_MS  100

#define REG_INCREMENT       0x10
#define REG_BUTTON          0x20
#define REG_COUNTER_RESET   0x40

enum { BTN_S, BTN_1, BTN_2, BTN_COUNT };

static i2c_master_dev_handle_t s_dev;
static lv_indev_t *s_indev;
static uint8_t s_btn_idle[BTN_COUNT];
static int32_t s_enc_acc;
static uint32_t s_last_key = LV_KEY_ENTER;

static bool s_disabled;   /* stop touching the module after any bus error */

static esp_err_t hmi_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_disabled) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, HMI_TIMEOUT_MS);
    if (err != ESP_OK) {
        /* Don't keep hammering a shared bus the PMIC and RTC depend on. */
        ESP_LOGE(TAG, "Read 0x%02x failed (%s); encoder disabled", reg, esp_err_to_name(err));
        s_disabled = true;
    }
    return err;
}

/** Read the three buttons, one 1-byte transfer each. */
static esp_err_t hmi_read_buttons(uint8_t out[BTN_COUNT])
{
    for (int i = 0; i < BTN_COUNT; i++) {
        esp_err_t err = hmi_read(REG_BUTTON + i, &out[i], 1);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t hmi_write_u8(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), HMI_TIMEOUT_MS);
}

static void encoder_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t inc[4];
    uint8_t btn[BTN_COUNT];

    data->enc_diff = 0;
    data->key = s_last_key;
    data->state = LV_INDEV_STATE_RELEASED;

    if (hmi_read(REG_INCREMENT, inc, sizeof(inc)) == ESP_OK) {
        int32_t delta = (int32_t)((uint32_t)inc[0] | ((uint32_t)inc[1] << 8) |
                                  ((uint32_t)inc[2] << 16) | ((uint32_t)inc[3] << 24));
#if CONFIG_GIZMO_HMI_REVERSE
        delta = -delta;
#endif
        if (delta) {
            ESP_LOGD(TAG, "raw delta %" PRId32, delta);
        }
        /* Accumulate so encoders with several counts per detent move one step per click. */
        s_enc_acc += delta;
        int32_t steps = s_enc_acc / CONFIG_GIZMO_HMI_COUNTS_PER_STEP;
        s_enc_acc -= steps * CONFIG_GIZMO_HMI_COUNTS_PER_STEP;
        data->enc_diff = (int16_t)steps;
    }

    if (hmi_read_buttons(btn) == ESP_OK) {
        if (btn[BTN_S] != s_btn_idle[BTN_S]) {
            s_last_key = LV_KEY_ENTER;
            data->state = LV_INDEV_STATE_PRESSED;
        } else if (btn[BTN_1] != s_btn_idle[BTN_1]) {
            s_last_key = LV_KEY_ESC;
            data->state = LV_INDEV_STATE_PRESSED;
        } else if (btn[BTN_2] != s_btn_idle[BTN_2]) {
            s_last_key = LV_KEY_ENTER;
            data->state = LV_INDEV_STATE_PRESSED;
        }
        /* On release keep reporting the last key so LVGL pairs it with the press. */
        data->key = s_last_key;
    }
}

esp_err_t hmi_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(BSP_I2C_NUM, &bus), TAG, "no I2C bus");

    /* The module's MCU can still be starting when we boot; give it time. */
    int tries = 0;
    while (i2c_master_probe(bus, HMI_ADDR, 50) != ESP_OK) {
        if (++tries >= HMI_PROBE_TRIES) {
            ESP_LOGW(TAG, "Module HMI not found at 0x%02x after %d ms, encoder disabled",
                     HMI_ADDR, tries * HMI_PROBE_DELAY_MS);
            return ESP_ERR_NOT_FOUND;
        }
        vTaskDelay(pdMS_TO_TICKS(HMI_PROBE_DELAY_MS));
    }
    if (tries) {
        ESP_LOGI(TAG, "Module HMI answered after %d ms", tries * HMI_PROBE_DELAY_MS);
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HMI_ADDR,
        .scl_speed_hz = HMI_I2C_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device failed");

    /* Clear stale counts, then learn the idle button level: the protocol only
     * says buttons read 0~1, not which level means pressed. */
    hmi_write_u8(REG_COUNTER_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));   /* let the module's MCU process the write */
    uint8_t inc[4];
    ESP_RETURN_ON_ERROR(hmi_read(REG_INCREMENT, inc, sizeof(inc)), TAG, "increment read failed");
    ESP_RETURN_ON_ERROR(hmi_read_buttons(s_btn_idle), TAG, "button read failed");
    ESP_LOGI(TAG, "Module HMI found, idle buttons S=%u 1=%u 2=%u",
             s_btn_idle[BTN_S], s_btn_idle[BTN_1], s_btn_idle[BTN_2]);

    bsp_display_lock(0);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_indev, encoder_read_cb);
    bsp_display_unlock();

    return ESP_OK;
}

lv_indev_t *hmi_get_indev(void)
{
    return s_indev;
}

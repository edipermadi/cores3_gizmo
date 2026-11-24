/*
 * Battery status and charge control for the CoreS3's AXP2101 PMIC.
 *
 * The BSP talks to the same chip for the backlight but exposes no battery API,
 * so we add our own device handle on the shared bus (the I2C master driver
 * serialises access per bus).
 *
 * A background task polls the PMIC once a second, enables battery charging
 * only while USB/bus 5 V is present, and caches the result so UI code can read
 * it without doing I2C.
 *
 * Registers used (AXP2101 datasheet):
 *   0x00 PMU status 1   bit5 VBUS good, bit3 battery present
 *   0x01 PMU status 2   bits[6:5] battery current: 00 standby, 01 charge, 10 discharge
 *   0x18 charger/gauge  bit3 fuel gauge enable, bit1 cell battery charge enable
 *   0xA4 battery %      fuel gauge state of charge, 0..100
 *   0x10 common config  bit0 software power-off
 *   0x41 IRQ enable 2   bit3 power key short press
 *   0x48-0x4A IRQ status (write 1 to clear); 0x49 bit3 power key short press
 *
 * Power key (left side button, AXP2101 PWRON): a short press powers the device
 * off. A long press still does the PMIC's own hard power-off. Presses during
 * the first BOOT_GRACE_MS are ignored, so the press used to switch the device
 * on can't immediately switch it off again.
 */
#include "power.h"

#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

#define AXP2101_ADDR        0x34
#define AXP_TIMEOUT_MS      50
#define POLL_MS             1000    /* battery / charge policy */
#define KEY_POLL_MS         100     /* power key responsiveness */
#define BOOT_GRACE_MS       3000

#define REG_STATUS1         0x00
#define REG_STATUS2         0x01
#define REG_CHG_GAUGE_CTRL  0x18
#define REG_BATT_PERCENT    0xA4
#define REG_COMMON_CFG      0x10
#define REG_IRQ_EN2         0x41
#define REG_IRQ_STS1        0x48
#define REG_IRQ_STS2        0x49
#define REG_IRQ_STS3        0x4A

#define STATUS1_VBUS_GOOD   (1 << 5)
#define STATUS1_BATT_EXIST  (1 << 3)
#define CTRL_GAUGE_EN       (1 << 3)
#define CTRL_CHARGE_EN      (1 << 1)
#define CFG_SOFT_PWROFF     (1 << 0)
#define IRQ2_PKEY_SHORT     (1 << 3)

static i2c_master_dev_handle_t s_axp;
static gizmo_battery_t s_state = { .percent = -1 };
static bool s_valid;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t axp_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_axp, &reg, 1, val, 1, AXP_TIMEOUT_MS);
}

static esp_err_t axp_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_axp, buf, sizeof(buf), AXP_TIMEOUT_MS);
}

/** Set or clear bits in a register, writing only if something changes. */
static esp_err_t axp_update_bits(uint8_t reg, uint8_t mask, bool set, bool *changed)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(axp_read(reg, &v), TAG, "read 0x%02x failed", reg);
    uint8_t nv = set ? (v | mask) : (v & ~mask);
    if (changed) {
        *changed = nv != v;
    }
    return nv == v ? ESP_OK : axp_write(reg, nv);
}

/** Read PMIC state and apply the charge policy: charge only on external power. */
static esp_err_t poll_once(gizmo_battery_t *out)
{
    uint8_t st1, st2, pct;
    ESP_RETURN_ON_ERROR(axp_read(REG_STATUS1, &st1), TAG, "status1 read failed");

    gizmo_battery_t b = { .percent = -1 };
    b.external_power = (st1 & STATUS1_VBUS_GOOD) != 0;
    b.present = (st1 & STATUS1_BATT_EXIST) != 0;

    bool changed = false;
    ESP_RETURN_ON_ERROR(axp_update_bits(REG_CHG_GAUGE_CTRL, CTRL_CHARGE_EN,
                                        b.external_power && b.present, &changed),
                        TAG, "charge control failed");
    if (changed) {
        ESP_LOGI(TAG, "Battery charging %s", b.external_power && b.present ? "enabled" : "disabled");
    }

    /* Read status 2 after the policy write so `charging` reflects it. */
    ESP_RETURN_ON_ERROR(axp_read(REG_STATUS2, &st2), TAG, "status2 read failed");
    b.charging = b.present && ((st2 >> 5) & 0x03) == 0x01;

    if (b.present && axp_read(REG_BATT_PERCENT, &pct) == ESP_OK) {
        b.percent = pct > 100 ? 100 : pct;
    }

    *out = b;
    return ESP_OK;
}

/** Clear every latched PMIC interrupt flag (write-1-to-clear). */
static void clear_irqs(void)
{
    axp_write(REG_IRQ_STS1, 0xFF);
    axp_write(REG_IRQ_STS2, 0xFF);
    axp_write(REG_IRQ_STS3, 0xFF);
}

void power_off(void)
{
    ESP_LOGW(TAG, "Powering off");
    /* Backlight off first so the screen goes dark immediately, then cut power. */
    bsp_display_brightness_set(0);
    bsp_display_backlight_off();
    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t cfg = 0;
    axp_read(REG_COMMON_CFG, &cfg);
    axp_write(REG_COMMON_CFG, cfg | CFG_SOFT_PWROFF);

    /* Still running: USB power can keep the system up on some boards. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "Power-off did not take effect (external power?)");
}

/** True once if the power key was short-pressed since the last check. */
static bool power_key_pressed(void)
{
    uint8_t sts;
    if (axp_read(REG_IRQ_STS2, &sts) != ESP_OK || !(sts & IRQ2_PKEY_SHORT)) {
        return false;
    }
    axp_write(REG_IRQ_STS2, IRQ2_PKEY_SHORT);
    return true;
}

static void power_task(void *arg)
{
    (void)arg;
    const TickType_t grace_end = xTaskGetTickCount() + pdMS_TO_TICKS(BOOT_GRACE_MS);
    bool grace = true;
    int ticks_to_poll = 0;

    while (true) {
        if (grace && xTaskGetTickCount() >= grace_end) {
            /* Drop anything left over from the power-on press. */
            clear_irqs();
            grace = false;
        }
        if (!grace && power_key_pressed()) {
            ESP_LOGI(TAG, "Power key short press");
            power_off();
        }

        if (--ticks_to_poll <= 0) {
            ticks_to_poll = POLL_MS / KEY_POLL_MS;
            gizmo_battery_t b;
            bool ok = poll_once(&b) == ESP_OK;

            taskENTER_CRITICAL(&s_lock);
            if (ok) {
                s_state = b;
            }
            s_valid = ok;
            taskEXIT_CRITICAL(&s_lock);
        }

        vTaskDelay(pdMS_TO_TICKS(KEY_POLL_MS));
    }
}

esp_err_t power_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(BSP_I2C_NUM, &bus), TAG, "no I2C bus");

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_axp), TAG, "add device failed");

    ESP_RETURN_ON_ERROR(axp_update_bits(REG_CHG_GAUGE_CTRL, CTRL_GAUGE_EN, true, NULL),
                        TAG, "enable gauge failed");

    /* Power key: latch short presses, and start from a clean slate. */
    ESP_RETURN_ON_ERROR(axp_update_bits(REG_IRQ_EN2, IRQ2_PKEY_SHORT, true, NULL),
                        TAG, "enable power key IRQ failed");
    clear_irqs();

    /* First poll synchronously so the UI has real data on its first frame. */
    gizmo_battery_t b;
    if (poll_once(&b) == ESP_OK) {
        s_state = b;
        s_valid = true;
    }

    BaseType_t ok = xTaskCreate(power_task, "power", 3072, NULL, 3, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task create failed");
    return ESP_OK;
}

esp_err_t power_get_battery(gizmo_battery_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "null out");

    taskENTER_CRITICAL(&s_lock);
    bool valid = s_valid;
    *out = s_state;
    taskEXIT_CRITICAL(&s_lock);

    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

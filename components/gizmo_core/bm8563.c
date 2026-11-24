/*
 * BM8563 RTC driver (PCF8563 register layout).
 *
 *   0x00 control 1   bit5 STOP (oscillator stopped)
 *   0x02 seconds     bit7 VL (voltage low: time invalid), BCD 0-59
 *   0x03 minutes     BCD 0-59
 *   0x04 hours       BCD 0-23
 *   0x05 day         BCD 1-31
 *   0x06 weekday     0-6
 *   0x07 month       bit7 century, BCD 1-12
 *   0x08 year        BCD 0-99 (we store 2000 + year, century bit 0)
 */
#include "bm8563.h"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rtc";

#define BM8563_ADDR     0x51
#define RTC_TIMEOUT_MS  50

#define REG_CTRL1       0x00
#define REG_SECONDS     0x02

#define SEC_VL          0x80
#define CTRL1_STOP      0x20

/* Anything earlier is a reset RTC, not a real date. */
#define MIN_VALID_YEAR  2024

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/** Days since 1970-01-01 for a proleptic Gregorian date (newlib has no timegm). */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

esp_err_t bm8563_init(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(i2c_master_get_bus_handle(BSP_I2C_NUM, &bus), TAG, "no I2C bus");

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BM8563_ADDR,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device failed");

    uint8_t reg = REG_CTRL1, ctrl1;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, &ctrl1, 1, RTC_TIMEOUT_MS),
                        TAG, "RTC not responding");
    if (ctrl1 & CTRL1_STOP) {
        ESP_LOGW(TAG, "Oscillator was stopped, starting it");
        const uint8_t buf[2] = { REG_CTRL1, 0x00 };
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, buf, sizeof(buf), RTC_TIMEOUT_MS), TAG, "start failed");
    }
    return ESP_OK;
}

esp_err_t bm8563_read(time_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    uint8_t reg = REG_SECONDS, r[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), RTC_TIMEOUT_MS),
                        TAG, "read failed");

    if (r[0] & SEC_VL) {
        ESP_LOGW(TAG, "RTC lost power; time not valid");
        return ESP_ERR_INVALID_STATE;
    }

    int sec = bcd2bin(r[0] & 0x7F);
    int min = bcd2bin(r[1] & 0x7F);
    int hour = bcd2bin(r[2] & 0x3F);
    int day = bcd2bin(r[3] & 0x3F);
    int mon = bcd2bin(r[5] & 0x1F);
    int year = 2000 + bcd2bin(r[6]);

    if (year < MIN_VALID_YEAR || mon < 1 || mon > 12 || day < 1 || day > 31 ||
        hour > 23 || min > 59 || sec > 59) {
        ESP_LOGW(TAG, "RTC holds no valid date (%04d-%02d-%02d)", year, mon, day);
        return ESP_ERR_INVALID_STATE;
    }

    *out = (time_t)(days_from_civil(year, mon, day) * 86400 + hour * 3600 + min * 60 + sec);
    return ESP_OK;
}

esp_err_t bm8563_write(time_t t)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");

    struct tm tm;
    gmtime_r(&t, &tm);
    ESP_RETURN_ON_FALSE(tm.tm_year >= 100 && tm.tm_year < 200, ESP_ERR_INVALID_ARG, TAG, "year out of range");

    const uint8_t buf[8] = {
        REG_SECONDS,
        bin2bcd(tm.tm_sec),            /* VL cleared */
        bin2bcd(tm.tm_min),
        bin2bcd(tm.tm_hour),
        bin2bcd(tm.tm_mday),
        (uint8_t)tm.tm_wday,
        bin2bcd(tm.tm_mon + 1),        /* century bit 0 = 20xx */
        bin2bcd(tm.tm_year - 100),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), RTC_TIMEOUT_MS);
}

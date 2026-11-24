/*
 * `i2cscan`: probe the internal I2C bus and print responding addresses with
 * known device names (same data as the I2C page, without the UI).
 */
#include <stdio.h>

#include "console_priv.h"
#include "esp_console.h"
#include "gizmo_core.h"

static int i2cscan_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t addrs[112];
    int n = gizmo_core_i2c_scan(addrs, sizeof(addrs));
    if (n < 0) {
        printf("I2C bus not available\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        const char *name = gizmo_core_i2c_device_name(addrs[i]);
        printf("0x%02X  %s\n", addrs[i], name ? name : "?");
    }
    printf("%d device(s)\n", n);
    return 0;
}

esp_err_t cmd_i2c_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "i2cscan",
        .help = "List devices answering on the internal I2C bus",
        .func = i2cscan_cmd,
    };
    return esp_console_cmd_register(&cmd);
}

/*
 * `led <index|all|off> [r g b]`: drive the base RGB LEDs directly, e.g. to
 * find which index is where (`led 0`, `led 4`, `led 5` ...). Default colour
 * is dim white. The spectrum page's VU meter overwrites them while it runs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console_priv.h"
#include "esp_console.h"
#include "gizmo_core.h"

static int led_cmd(int argc, char **argv)
{
    int n = gizmo_core_leds_count();
    if (n == 0) {
        printf("no LEDs available\n");
        return 1;
    }
    if (argc < 2) {
        printf("usage: led <0..%d|all|off> [r g b]\n", n - 1);
        return 1;
    }
    if (strcmp(argv[1], "off") == 0) {
        gizmo_core_leds_off();
        printf("leds off\n");
        return 0;
    }

    int r = 40, g = 40, b = 40;
    if (argc >= 5) {
        r = atoi(argv[2]);
        g = atoi(argv[3]);
        b = atoi(argv[4]);
    }
    if (strcmp(argv[1], "all") == 0) {
        for (int i = 0; i < n; i++) {
            gizmo_core_leds_set(i, r, g, b);
        }
        printf("all %d leds = %d,%d,%d\n", n, r, g, b);
        return 0;
    }

    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= n) {
        printf("index out of range 0..%d\n", n - 1);
        return 1;
    }
    gizmo_core_leds_off();
    gizmo_core_leds_set(idx, r, g, b);
    printf("led %d = %d,%d,%d\n", idx, r, g, b);
    return 0;
}

esp_err_t cmd_led_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "led",
        .help = "led <index|all|off> [r g b]: light base RGB LEDs (one index lights only that LED)",
        .func = led_cmd,
    };
    return esp_console_cmd_register(&cmd);
}

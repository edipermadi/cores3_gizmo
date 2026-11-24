#include "gizmo_console.h"

#include "console_priv.h"
#include "esp_check.h"
#include "esp_console.h"

static const char *TAG = "console";

esp_err_t gizmo_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "gizmo>";
    /* screenshot renders (LVGL software draw) and deflates on this stack */
    repl_cfg.task_stack_size = 16384;

    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl), TAG, "repl");

    ESP_RETURN_ON_ERROR(esp_console_register_help_command(), TAG, "help");
    ESP_RETURN_ON_ERROR(cmd_screenshot_register(), TAG, "screenshot");
    ESP_RETURN_ON_ERROR(cmd_touch_register(), TAG, "touch");
    ESP_RETURN_ON_ERROR(cmd_i2c_register(), TAG, "i2cscan");

    return esp_console_start_repl(repl);
}

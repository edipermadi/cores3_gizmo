#include "esp_log.h"
#include "gizmo_core.h"
#include "gizmo_ui.h"

static const char *TAG = "gizmo";

void app_main(void)
{
    /* Don't abort on failure: restarting can't fix a stuck bus, it just
     * boot-loops and buries the error message. Log and idle instead. */
    esp_err_t err = gizmo_core_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed (%s); UI not started", esp_err_to_name(err));
        return;
    }

    gizmo_core_lock(0);
    gizmo_ui_init();
    gizmo_core_unlock();

    ESP_LOGI(TAG, "UI started, brightness %d%%", gizmo_core_get_brightness());
}

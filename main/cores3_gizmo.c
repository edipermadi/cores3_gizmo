#include "esp_log.h"
#include "gizmo_core.h"
#include "gizmo_ui.h"

static const char *TAG = "gizmo";

void app_main(void)
{
    ESP_ERROR_CHECK(gizmo_core_init());

    gizmo_core_lock(0);
    gizmo_ui_init();
    gizmo_core_unlock();

    ESP_LOGI(TAG, "UI started, brightness %d%%", gizmo_core_get_brightness());
}

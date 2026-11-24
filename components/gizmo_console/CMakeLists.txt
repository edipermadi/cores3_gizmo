idf_component_register(SRCS "gizmo_console.c" "cmd_screenshot.c" "cmd_touch.c" "cmd_i2c.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console esp_rom esp_timer gizmo_core)

idf_component_register(SRCS "gizmo_core.c" "settings.c" "hmi.c" "power.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash esp_driver_i2c esp_driver_gpio)

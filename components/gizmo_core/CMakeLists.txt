idf_component_register(SRCS "gizmo_core.c" "settings.c" "hmi.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash esp_driver_i2c)

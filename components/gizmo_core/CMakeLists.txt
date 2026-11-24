idf_component_register(SRCS "gizmo_core.c" "settings.c" "hmi.c" "power.c" "wifi.c" "timesync.c" "bm8563.c" "timezones.c" "spectrum.c" "leds.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash esp_driver_i2c
                                     esp_wifi esp_netif esp_event esp_timer)

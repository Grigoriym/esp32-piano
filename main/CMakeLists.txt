idf_component_register(SRCS "main.c" "ble_midi.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_driver_gpio esp_rom esp_timer bt nvs_flash)

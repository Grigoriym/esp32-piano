idf_component_register(SRCS "main.c" "matrix.c" "keymap.c" "demo.c" "looper.c" "rhythm.c" "wifi.c" "midi_link.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_driver_gpio esp_rom esp_timer esp_wifi esp_netif esp_event nvs_flash lwip mdns)

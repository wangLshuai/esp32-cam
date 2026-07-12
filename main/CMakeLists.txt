file(GLOB_RECURSE ble_src ble/*.c)
idf_component_register(SRCS "esp32-cam.c" "config_in_nvs.c" "${ble_src}"
                    INCLUDE_DIRS "." "./ble"
                    REQUIRES esp_psram esp_event esp_wifi nvs_flash lwip esp_timer 
                    bt
                    )

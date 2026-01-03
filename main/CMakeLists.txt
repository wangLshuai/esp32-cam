idf_component_register(SRCS "esp32-cam.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_psram esp_event esp_wifi nvs_flash lwip
                    )

#ifndef __CONFIG_IN_NVS_H__
#define __CONFIG_IN_NVS_H__
#include "esp_system.h"

void nvs_config_init();
esp_err_t nvs_save_wifi_creds(const char * ssid, const char * password);
esp_err_t nvs_load_wifi_creds(char * ssid, size_t ssid_len, char * password,
                              size_t pass_len);
bool nvs_load_wifi_force_ble_flag();
void nvs_set_wifi_force_ble_flag(uint8_t value);
#endif
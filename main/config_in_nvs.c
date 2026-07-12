// ============================================================================
// NVS WiFi Credential Storage
// ============================================================================
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "config_in_nvs.h"

// NVS keys for WiFi credentials
#define NVS_WIFI_NAMESPACE "wifi_creds"
#define NVS_KEY_FORCE_BLE "force_ble"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define TAG "config"

void nvs_config_init()
{
    // Initialize NVS first (required for WiFi credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Ensure wifi_creds namespace is initialized on first boot
    nvs_handle_t handle;
    ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG,
                 "NVS wifi_creds namespace not found, initializing from "
                 "menuconfig...");
        ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
        if (ret == ESP_OK) {
            nvs_set_str(handle, NVS_KEY_SSID, CONFIG_WIFI_SSID);
            nvs_set_str(handle, NVS_KEY_PASS, CONFIG_WIFI_PASSWORD);
            nvs_set_u8(handle, NVS_KEY_FORCE_BLE, 0);
            nvs_commit(handle);
            nvs_close(handle);
            ESP_LOGI(
                TAG, "Initialized NVS with SSID: %s",
                strlen(CONFIG_WIFI_SSID) > 0 ? CONFIG_WIFI_SSID : "(empty)");
        }
    } else {
        nvs_close(handle);
    }
}

esp_err_t nvs_save_wifi_creds(const char * ssid, const char * password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", err);
        return err;
    }
    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str ssid failed: %d", err);
        nvs_close(handle);
        return err;
    }
    err = nvs_set_str(handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str password failed: %d", err);
        nvs_close(handle);
        return err;
    }
    err = nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    return err;
}

esp_err_t nvs_load_wifi_creds(char * ssid, size_t ssid_len, char * password,
                              size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t len = ssid_len;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    len = pass_len;
    err = nvs_get_str(handle, NVS_KEY_PASS, password, &len);
    nvs_close(handle);
    return err;
}

bool nvs_load_wifi_force_ble_flag()
{
    uint8_t force_ble_flag = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(handle, NVS_KEY_FORCE_BLE, &force_ble_flag);
    if (err != ESP_OK) {
        nvs_close(handle);
        return 0;
    }

    nvs_close(handle);
    return force_ble_flag;
}

void nvs_set_wifi_force_ble_flag(uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        if (ret == ESP_OK) {
            nvs_set_u8(handle, NVS_KEY_FORCE_BLE, value);
            nvs_commit(handle);
            nvs_close(handle);
            ESP_LOGI(TAG, "set nvs wifi force ble flag: %b", value);
        }
    } else {
        nvs_close(handle);
    }
}
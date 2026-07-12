#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "hal/gpio_types.h"
#include "mdns.h"

#include "lwip/err.h"
#include "lwip/sockets.h"

#include <stdint.h>
#include <string.h>

#include "ble.h"
#include "config_in_nvs.h"

#define TAG "esp32-cam"
#define PORT 3488

// Event group bits for WiFi connection synchronization
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group;

static void button_init()
{
    int gpio = CONFIG_WIFI_PROV_BUTTON_GPIO;
    int active_level = CONFIG_WIFI_PROV_BUTTON_ACTIVE_LEVEL;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en =
            (active_level == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLDOWN_ENABLE,
        .pull_down_en =
            (active_level == 0) ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&io_conf);
}
// ============================================================================
// Button Long-Press Detection
// ============================================================================

static bool is_key_long_pressed(void)
{
    int gpio = CONFIG_WIFI_PROV_BUTTON_GPIO;
    int active_level = CONFIG_WIFI_PROV_BUTTON_ACTIVE_LEVEL;
    // Check if button is initially pressed
    if (gpio_get_level(gpio) != active_level) {
        return false;
    }

    ESP_LOGI(TAG, "Button pressed, checking for 5s long press...");

    // Wait for 5 continuous seconds of press
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < 5000000) {
        if (gpio_get_level(gpio) != active_level) {
            ESP_LOGI(TAG, "Button released before 5s, normal boot");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Button long press detected! Forcing BLE provisioning");
    return true;
}

// ============================================================================
// Camera Configuration
// ============================================================================

static camera_config_t camera_config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = 15,
    .pin_sccb_sda = 4,
    .pin_sccb_scl = 5,

    .pin_d7 = 16,
    .pin_d6 = 17,
    .pin_d5 = 18,
    .pin_d4 = 12,
    .pin_d3 = 10,
    .pin_d2 = 8,
    .pin_d1 = 9,
    .pin_d0 = 11,
    .pin_vsync = 6,
    .pin_href = 7,
    .pin_pclk = 13,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,

    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static esp_err_t init_camera(void)
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }
    return ESP_OK;
}

// ============================================================================
// mDNS Service
// ============================================================================

static void start_mdns_service(void)
{
    const char * hostname = "esp32-cam";
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: %s.local", hostname);
}

static void stop_mdns_service(void)
{
    mdns_free();
}

// ============================================================================
// WiFi Event Handler (for both direct connect and post-provisioning reconnect)
// ============================================================================

static void wifi_event_handler(void * arg, esp_event_base_t event_base,
                               int32_t event_id, void * event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t * event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============================================================================
// Direct WiFi Connection (using NVS credentials)
// ============================================================================

static esp_err_t wifi_connect(const char * ssid, const char * password)
{
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password,
            sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    // Block until connected (disconnect handler retries indefinitely)
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        portMAX_DELAY);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &wifi_event_handler);
    vEventGroupDelete(s_wifi_event_group);
    ESP_LOGI(TAG, "WiFi connected successfully");
    return ESP_OK;
}

static QueueHandle_t key_evt_queue = NULL;
static void IRAM_ATTR key_isr_handler(void * arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(key_evt_queue, &gpio_num, NULL);
}

void long_push_key_task()
{
    uint32_t io_num;
    bool key_push = false;
    uint8_t count = 0;
    while (1) {
        if (xQueueReceive(key_evt_queue, &io_num, pdMS_TO_TICKS(1000))) {
            ESP_LOGI(
                TAG,
                "long push gpio %d key, restart enter force ble provision mode",
                CONFIG_WIFI_PROV_BUTTON_GPIO);
            if (gpio_get_level(io_num) ==
                CONFIG_WIFI_PROV_BUTTON_ACTIVE_LEVEL) {
                key_push = true;
            } else {
                key_push = false;
                count = 0;
            }
        }
        if (key_push && count < 5) {
            if (++count == 5) {
                nvs_set_wifi_force_ble_flag(1);
                esp_restart();
            }
        }
    }
}

// ============================================================================
// Main Application
// ============================================================================

void app_main(void)
{
    key_evt_queue = xQueueCreate(4, sizeof(uint32_t));
    button_init();
    nvs_config_init();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    start_ble();

    // Create default WiFi STA netif
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // Determine provisioning path
    bool force_ble = is_key_long_pressed();

    if (!force_ble) {
        force_ble = nvs_load_wifi_force_ble_flag();
    }

    // 运行时，按键长按，则置位 nvs force ble flag key, 并重启。
    xTaskCreatePinnedToCore(long_push_key_task, "push button task",
                            ESP_TASK_MAIN_STACK, NULL, ESP_TASK_MAIN_PRIO, NULL,
                            ESP_TASK_MAIN_CORE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(CONFIG_WIFI_PROV_BUTTON_GPIO, key_isr_handler,
                         (void *)CONFIG_WIFI_PROV_BUTTON_GPIO);

    if (!force_ble) {
        char ssid[33] = {0};
        char password[65] = {0};
        if (nvs_load_wifi_creds(ssid, sizeof(ssid), password,
                                sizeof(password)) == ESP_OK &&
            strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Using WiFi credentials from NVS");
            ESP_ERROR_CHECK(wifi_connect(ssid, password));
        } else {
            ESP_LOGI(TAG, "No NVS credentials, starting BLE provisioning");
            force_ble = true;
        }
    }

    nvs_set_wifi_force_ble_flag(0);
    // Disable WiFi power saving for better responsiveness
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Start mDNS service
    start_mdns_service();

    // Create TCP listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);

    ESP_LOGI(TAG, "Socket created");

    int err =
        bind(listen_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    if (ESP_OK != init_camera()) {
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        int sock =
            accept(listen_sock, (struct sockaddr *)&peer_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }
        int nodelay = 1;
        err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                         sizeof(nodelay));
        if (err != 0) {
            ESP_LOGE(TAG, "setsockopt TCP_NODELAY failed: %s", strerror(errno));
        }
        ssize_t ret;
        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(100);
        do {
            ESP_LOGI(TAG, "Taking picture...");
            int64_t start = esp_timer_get_time();
            camera_fb_t * pic = esp_camera_fb_get();

            int64_t end = esp_timer_get_time();
            ret = 0;
            if (pic) {
                ESP_LOGI(TAG,
                         "Picture taken! duration:%lld us. size: %zu "
                         "width:%zu height:%zu format:%d",
                         end - start, pic->len, pic->width, pic->height,
                         pic->format);

                /* header: length(4B BE) + width(2B BE) + height(2B BE)
                 * + format(1B) = 9 bytes */
                uint8_t header[9];
                uint32_t len_be = htonl(pic->len);
                uint16_t width_be = htons(pic->width);
                uint16_t height_be = htons(pic->height);
                memcpy(header, &len_be, 4);
                memcpy(header + 4, &width_be, 2);
                memcpy(header + 6, &height_be, 2);
                header[8] = pic->format;

                int64_t send_start = esp_timer_get_time();
                ret = send(sock, header, sizeof(header), 0);
                if (ret > 0) {
                    ret = send(sock, pic->buf, pic->len, 0);
                    if (ret <= 0) {
                        ESP_LOGI(TAG, "ret:%d\n", ret);
                    }
                } else {
                    ESP_LOGI(TAG, "ret:%d\n", ret);
                }
                int64_t send_end = esp_timer_get_time();
                ESP_LOGI(TAG, "send n:%d duration:%lld", ret,
                         send_end - send_start);
                esp_camera_fb_return(pic);
            }
            // 25fps (40ms)
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        } while (ret > 0);
        if (sock > 0) {
            close(sock);
        }
    }

CLEAN_UP:
    close(listen_sock);
    return;
}

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"

#define TAG "esp32-cam"
#define PORT 3488

static void start_mdns_service(void)
{
    const char * hostname = "esp32-cam";
    // initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    // set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: %s.local", hostname);
}

static void stop_mdns_service(void)
{
    mdns_free();
}

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

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565,  // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size =
        FRAMESIZE_QVGA,  // QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the
                         // ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12,  // 0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,       // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static esp_err_t init_camera(void)
{
    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    esp_wifi_set_ps(WIFI_PS_NONE);

    start_mdns_service();

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

    int err = bind(listen_sock, (struct sockaddr *)&addr, sizeof(struct sockaddr));
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

        struct sockaddr_in peer_addr;  // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(peer_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&peer_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }
        int nodelay = 1;
        err = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        if (err != 0) {
            ESP_LOGE(TAG, "setsockopt TCP_NODELAY failed: %s", strerror(errno));
        }
        ssize_t ret;
        do {
            ESP_LOGI(TAG, "Taking picture...");
            int64_t start = esp_timer_get_time();
            camera_fb_t * pic = esp_camera_fb_get();

            int64_t end = esp_timer_get_time();
            ret = 0;
            // use pic->buf to access the image
            if (pic) {
                ESP_LOGI(TAG, "Picture taken! duration:%lld us .Its size was: %zu widty:%zu height:%zu bytes,format:%d",
                         end - start, pic->len, pic->width, pic->height, pic->format);
                // char buf[64];
                // size_t n = snprintf(buf,64,"bytes:%u\n",pic->len);
                start = esp_timer_get_time();
                ret = send(sock, pic->buf, pic->len, 0);
                end = esp_timer_get_time();
                ESP_LOGI(TAG, "send n:%d duration:%lld", ret, end - start);
                esp_camera_fb_return(pic);
            }
        } while (ret > 0);
        if (sock > 0) {
            close(sock);
        }
    }

CLEAN_UP:
    close(listen_sock);
    return;
}

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define LED_GPIO 14
static const char *TAG = "WachcioDrop_Server";

void blink_task(void *pvParameter)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Zapisz dane Wi-Fi do NVS
void save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_str(nvs_handle, "ssid", ssid);
        nvs_set_str(nvs_handle, "password", password);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi creds saved to NVS");
    } else {
        ESP_LOGE(TAG, "Error opening NVS");
    }
}

// Handler HTTP GET dla root (formularz Wi-Fi)
esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* resp_str = "<html><body><h1>WachcioDrop Provisioning Portal</h1>"
                           "<form action=\"/submit\" method=\"post\">"
                           "SSID: <input type=\"text\" name=\"ssid\"><br>"
                           "Password: <input type=\"password\" name=\"password\"><br>"
                           "<input type=\"submit\" value=\"Save\">"
                           "</form>"
                           "</body></html>";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Ustawienia Wi-Fi z zapisanych danych i połącz się
void wifi_connect(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "WiFi connecting to SSID:%s", ssid);
    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password)-1);

    esp_wifi_stop();
    esp_wifi_deinit();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();
}

// Handler HTTP POST dla /submit (odbiera i zapisuje ssid i hasło)
esp_err_t submit_post_handler(httpd_req_t *req)
{
    char buf[128];
    int len = MIN(req->content_len, sizeof(buf) - 1);
    int ret = httpd_req_recv(req, buf, len);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // Proste parsowanie danych POST, formatu: ssid=name&password=pass
    char ssid[32] = {0};
    char password[64] = {0};
    sscanf(buf, "ssid=%31[^&]&password=%63s", ssid, password);

    ESP_LOGI(TAG, "Received SSID: %s, Password: %s", ssid, password);

    save_wifi_credentials(ssid, password);

    // Po zapisaniu próbujemy połączyć się z podaną siecią
    wifi_connect(ssid, password);

    const char* resp = "<html><body><h2>Settings saved. Connecting to WiFi...</h2></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

httpd_uri_t submit = {
    .uri       = "/submit",
    .method    = HTTP_POST,
    .handler   = submit_post_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &submit);
    }
    return server;
}

void wifi_init_softap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "WachcioDrop",
            .ssid_len = strlen("WachcioDrop"),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "SoftAP started. SSID:%s", wifi_config.ap.ssid);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(blink_task, "blink_task", 2048, NULL, 5, NULL);

    wifi_init_softap();
    start_webserver();
}

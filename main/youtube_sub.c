#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include "cJSON.h"

#define ESP32_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP32_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP32_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#define API_KEY         "AIzaSyCRuQWNdDq7vlEPlJXIxuBbjLKE0-goUkk"
#define CHANNEL_ID      "UCuCl93NjLSbGbJEF4IzGWRg"


//https://www.googleapis.com/youtube/v3/channels?id=UCuCl93NjLSbGbJEF4IzGWRg&key=AIzaSyCRuQWNdDq7vlEPlJXIxuBbjLKE0-goUkk&part=statistics
//https://www.googleapis.com/youtube/v3/channels?id=UCuCl93NjLSbGbJEF4IzGWRg&key=AIzaSyCRuQWNdDq7vlEPlJXIxuBbjLKE0-goUkk&part=snippet,statistics

//https://www.googleapis.com/youtube/v3/channels?id="CHANNEL_ID"&key="API_KEY"&part=statistics
//https://www.googleapis.com/youtube/v3/channels?id="CHANNEL_ID"&key="API_KEY"&part=snippet,statistics


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "Youtube Project";

static int s_retry_num = 0;


static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP32_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t client_event_get_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_DATA:
            char *data_string = malloc(evt->data_len + 1);
            if (data_string == NULL) {
                break;
            }
            memcpy(data_string, evt->data, evt->data_len);
            data_string[evt->data_len] = '\0';  // Agregar el carácter nulo al final
            printf("HTTP_EVENT_ON_DATA: %s", data_string);
            free(data_string);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void rest_get() {
    esp_http_client_config_t config_get = {
        //.url = "https://www.googleapis.com/youtube/v3/channels?id="CHANNEL_ID"&key="API_KEY"&part=snippet,statistics",
        .url = "https://www.googleapis.com/youtube/v3/channels?id="CHANNEL_ID"&key="API_KEY"&part=statistics",
        .method = HTTP_METHOD_GET,
        .cert_pem = NULL,
        .event_handler = client_event_get_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config_get);
    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP32_WIFI_SSID,
            .password = ESP32_WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP32_WIFI_SSID, ESP32_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", ESP32_WIFI_SSID, ESP32_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    rest_get();
}
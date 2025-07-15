#include <stdio.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Wi-Fi 정보
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"
#define WEBSOCKET_URI "ws://192.168.1.35:5000/"

static const char *TAG = "ws_client";

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket 연결 완료!");
            esp_websocket_client_send_text((esp_websocket_client_handle_t)handler_args, "{\"msg\": \"hello\"}", strlen("{\"msg\": \"hello\"}"), portMAX_DELAY);
            break;
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGI(TAG, "수신 데이터[%d bytes]: %.*s", data->data_len, data->data_len, (char *)data->data_ptr);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket 연결 끊김.");
            break;
        default:
            break;
    }
}

// Wi-Fi 연결 함수
void wifi_init(void)
{
    // 표준 Wi-Fi 연결 코드 작성 (생략)
    // esp_wifi_init(), esp_wifi_set_mode(), esp_wifi_start() 등
}

void app_main(void)
{
    nvs_flash_init();
    wifi_init();

    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_URI,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);

    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        if (esp_websocket_client_is_connected(client)) {
            const char *msg = "{\"data\":\"ESP32에서 보낸 메시지\"}";
            esp_websocket_client_send_text(client, msg, strlen(msg), portMAX_DELAY);
            ESP_LOGI(TAG, "메시지 전송: %s", msg);
        }
    }
}

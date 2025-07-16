#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dht.h"
#include "driver/adc_common.h"
#include "driver/adc.h"

#define device_id 1
// Wi-Fi 정보
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"
#define WEBSOCKET_URI "ws://192.168.1.35:5000/"
#define MAX_RETRY 10

#define DHT_PIN         GPIO_NUM_16
#define LDR_ADC_CHANNEL ADC1_CHANNEL_6   // GPIO 34
#define VREF            1100             // ESP32 기준 내부 Vref (mV), 보통 1100~1150 사이
#define R_FIXED         10000            // 분압 회로에서 LDR과 직렬로 연결된 고정 저항 (10kΩ)
#define N_POINTS 100

#define SMOOTH_TABLE_FILE "/spiffs_image/temp2hum_smooth_100.bin"

static int s_retry_num = 0;
static const char *WIFI_TAG = "wifi";
static const char *TAG = "ws_client";

float temperature = 0;
float humidity = 0;
float temp_table[N_POINTS];
float hum_table[N_POINTS];


void load_smooth_model(const char* path) {
    FILE* f = fopen(path, "rb");
    fread(temp_table, sizeof(float), N_POINTS, f);
    fread(hum_table, sizeof(float), N_POINTS, f);
    fclose(f);
}

float predict_humidity(float temp) {
    int idx = 0;
    while (idx < N_POINTS - 2 && temp > temp_table[idx+1]) idx++;
    float x0 = temp_table[idx], x1 = temp_table[idx+1];
    float y0 = hum_table[idx], y1 = hum_table[idx+1];
    if (x1 == x0) return y0;
    float ratio = (temp - x0) / (x1 - x0);
    return y0 + ratio * (y1 - y0);
}

float compute_corrosion_probability(float RH, float Ev_lx) {
    float score = 0.00674 * RH + 0.00000192 * Ev_lx - 1.365521;
    float prob = 1.0f / (1.0f + expf(-score));
    return prob;
}

void read_dht_data(void) {
    int ret = dht_read_data(DHT_TYPE_DHT11, DHT_PIN, &humidity, &temperature);
    if (ret == ESP_OK) {
        ESP_LOGI("DHT", "온도: %.1f°C, 습도: %.1f%%", temperature, humidity);
    } else {
        ESP_LOGW("DHT", "DHT 센서 읽기 실패");
    }
}

void ldr_init(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
}

float read_ldr_lux(void) {
    int raw = adc1_get_raw(LDR_ADC_CHANNEL);
    
    float voltage = (float)raw * VREF / 4095.0;

    float ldr_resistance = (voltage * R_FIXED) / (3300.0 - voltage); // mV → Ω
    
    float lux = pow((500000.0 / ldr_resistance), (1.0 / 1.4));

    ESP_LOGI("LDR", "ADC: %d, 전압: %.2f mV, 저항: %.0f Ω, 조도: %.2f lux", raw, voltage, ldr_resistance, lux);
    return lux;
}


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "재연결 시도...");
        } else {
            ESP_LOGI(WIFI_TAG, "연결 실패");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "IP 주소 할당됨: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }
}

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

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());                // 네트워크 인터페이스 초기화
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // 이벤트 루프 생성
    esp_netif_create_default_wifi_sta();              // STA 모드 인터페이스 생성

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); // Wi-Fi 드라이버 기본 설정
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));              // Wi-Fi 드라이버 초기화

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // WPA2만 허용
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));          // STA 모드로 설정
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config)); // 설정 적용
    ESP_ERROR_CHECK(esp_wifi_start());                          // Wi-Fi 시작

    ESP_LOGI(WIFI_TAG, "wifi_init_sta 완료");
}

void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    load_smooth_model(SMOOTH_TABLE_FILE);

    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_URI,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
    esp_websocket_client_start(client);

    while (1) {
		read_dht_data();
        float lux = read_ldr_lux();
        
        float corrosion = 100 * compute_corrosion_probability(humidity, lux);
        float target_humidity = predict_humidity(temperature);
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (esp_websocket_client_is_connected(client)) {
		    char msg[128];
		    snprintf(msg, sizeof(msg),
		        "{\"device_id\":%d,\"illu\":%.2f,\"temp\":%.2f,\"hum\":%.2f,\"oxid\":%.2f}",
		        device_id,
		        lux,              // 조도
		        temperature,      // 온도
		        humidity,         // 습도
		        corrosion         // 부식확률(0~100)
		    );
		    esp_websocket_client_send_text(client, msg, strlen(msg), portMAX_DELAY);
		    ESP_LOGI(TAG, "메시지 전송: %s", msg);
		}
    }
}

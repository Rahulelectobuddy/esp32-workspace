#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "WIFI_MQTT_APP";

/* GPIO Pin Configuration for Onboard LED */
#define BLINK_GPIO GPIO_NUM_2

/* FreeRTOS Event Group for Wi-Fi Connection Status */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static uint8_t s_led_state = 0;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

/* Forward Declarations */
static void set_led_state(uint8_t state, bool publish_status);
static void heartbeat_task(void *pvParameters);

/* -------------------------------------------------------------------------- */
/* GPIO / LED Helper Functions                                                */
/* -------------------------------------------------------------------------- */
static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, s_led_state);
    ESP_LOGI(TAG, "GPIO %d initialized (Default LED State: OFF)", BLINK_GPIO);
}

static void set_led_state(uint8_t state, bool publish_status)
{
    s_led_state = state ? 1 : 0;
    gpio_set_level(BLINK_GPIO, s_led_state);
    ESP_LOGI(TAG, "LED State set to: %s", s_led_state ? "ON" : "OFF");

    if (publish_status && s_mqtt_client && s_mqtt_connected) {
        char status_payload[64];
        snprintf(status_payload, sizeof(status_payload), "{\"led_state\":\"%s\"}", s_led_state ? "ON" : "OFF");
        int msg_id = esp_mqtt_client_publish(s_mqtt_client, CONFIG_MQTT_TOPIC_LED_STATUS, status_payload, 0, 1, 0);
        ESP_LOGI(TAG, "Published LED status to [%s] (msg_id=%d): %s", CONFIG_MQTT_TOPIC_LED_STATUS, msg_id, status_payload);
    }
}

/* -------------------------------------------------------------------------- */
/* Wi-Fi Event Handler                                                         */
/* -------------------------------------------------------------------------- */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s...", CONFIG_ESP_WIFI_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_ESP_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retrying Wi-Fi connection (%d/%d)...", s_retry_num, CONFIG_ESP_WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi SSID: %s", CONFIG_ESP_WIFI_SSID);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Connected! Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi Station Initialization finished.");

    /* Wait for connection result */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", CONFIG_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to AP SSID: %s", CONFIG_ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* -------------------------------------------------------------------------- */
/* MQTT Event Handler & Callback                                              */
/* -------------------------------------------------------------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED to broker: %s", CONFIG_MQTT_BROKER_URI);
        s_mqtt_connected = true;

        /* Subscribe to LED control command topic */
        int msg_id = esp_mqtt_client_subscribe(client, CONFIG_MQTT_TOPIC_LED_SET, 0);
        ESP_LOGI(TAG, "Subscribed to topic [%s] (msg_id=%d)", CONFIG_MQTT_TOPIC_LED_SET, msg_id);

        /* Publish initial LED status */
        set_led_state(s_led_state, true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA received from topic: %.*s", (int)event->topic_len, event->topic);
        ESP_LOGI(TAG, "Payload (%d bytes): %.*s", (int)event->data_len, (int)event->data_len, event->data);

        /* Process LED control commands */
        if (event->topic_len == strlen(CONFIG_MQTT_TOPIC_LED_SET) &&
            strncmp(event->topic, CONFIG_MQTT_TOPIC_LED_SET, event->topic_len) == 0) {

            char command[16] = {0};
            int len = event->data_len < sizeof(command) - 1 ? event->data_len : sizeof(command) - 1;
            memcpy(command, event->data, len);
            command[len] = '\0';

            if (strcasecmp(command, "ON") == 0 || strcmp(command, "1") == 0 || strcasecmp(command, "TRUE") == 0) {
                set_led_state(1, true);
            } else if (strcasecmp(command, "OFF") == 0 || strcmp(command, "0") == 0 || strcasecmp(command, "FALSE") == 0) {
                set_led_state(0, true);
            } else if (strcasecmp(command, "TOGGLE") == 0) {
                set_led_state(!s_led_state, true);
            } else {
                ESP_LOGW(TAG, "Unknown LED command received: '%s'. (Use: ON, OFF, TOGGLE)", command);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        ESP_LOGI(TAG, "Other MQTT event id: %d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
    ESP_LOGI(TAG, "MQTT Client initialized & started.");
}

/* -------------------------------------------------------------------------- */
/* Periodic Heartbeat Task                                                    */
/* -------------------------------------------------------------------------- */
static void heartbeat_task(void *pvParameters)
{
    char heartbeat_payload[192];

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_HEARTBEAT_INTERVAL_SEC * 1000));

        if (s_mqtt_client && s_mqtt_connected) {
            uint32_t uptime_sec = esp_log_timestamp() / 1000;
            uint32_t free_heap = esp_get_free_heap_size();
            int8_t rssi = 0;

            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                rssi = ap_info.rssi;
            }

            snprintf(heartbeat_payload, sizeof(heartbeat_payload),
                     "{\"status\":\"online\",\"uptime_sec\":%" PRIu32 ",\"free_heap\":%" PRIu32 ",\"wifi_rssi\":%d,\"led_state\":\"%s\"}",
                     uptime_sec, free_heap, (int)rssi, s_led_state ? "ON" : "OFF");

            int msg_id = esp_mqtt_client_publish(s_mqtt_client, CONFIG_MQTT_TOPIC_HEARTBEAT, heartbeat_payload, 0, 0, 0);
            ESP_LOGI(TAG, "Sent Heartbeat to [%s] (msg_id=%d): %s", CONFIG_MQTT_TOPIC_HEARTBEAT, msg_id, heartbeat_payload);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Main Application Entry Point                                               */
/* -------------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "   ESP32 Wi-Fi & MQTT LED Control App      ");
    ESP_LOGI(TAG, "===========================================");

    /* Initialize NVS Flash Storage */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Configure GPIO for LED */
    configure_led();

    /* Initialize Wi-Fi Connection */
    wifi_init_sta();

    /* Initialize and Start MQTT Client */
    mqtt_app_start();

    /* Create Periodic Heartbeat FreeRTOS Task */
    xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
}

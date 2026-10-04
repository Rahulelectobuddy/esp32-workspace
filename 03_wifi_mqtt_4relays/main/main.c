#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "esp_timer.h"

static const char *TAG = "WIFI_MQTT_4RELAYS";

#define NUM_RELAYS 4
#define RELAY1_AUTO_OFF_TIMEOUT_US (60ULL * 1000ULL * 1000ULL) /* 1 minute = 60,000,000 us */

static esp_timer_handle_t s_relay1_auto_off_timer = NULL;

/* FreeRTOS event group to signal Wi-Fi connection */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

/* Structure for individual relay management */
typedef struct {
    int id;                /* 1 to 4 */
    gpio_num_t gpio;       /* GPIO pin number */
    bool state;            /* true = ON, false = OFF */
    char set_topic[64];    /* e.g., "esp32/relay/1/set" */
    char status_topic[64]; /* e.g., "esp32/relay/1/status" */
} relay_channel_t;

static relay_channel_t s_relays[NUM_RELAYS];
static char s_all_set_topic[64];
static char s_heartbeat_topic[64];

/* -------------------------------------------------------------------------- */
/* Relay Control Helper                                                       */
/* -------------------------------------------------------------------------- */
static void set_relay_hardware(int idx, bool state)
{
    s_relays[idx].state = state;
#if CONFIG_RELAY_ACTIVE_HIGH
    int level = state ? 1 : 0;
#else
    int level = state ? 0 : 1;
#endif
    gpio_set_level(s_relays[idx].gpio, level);
}

static void publish_relay_status(int idx)
{
    if (!s_mqtt_client || !s_mqtt_connected) {
        return;
    }

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"relay\":%d,\"gpio\":%d,\"state\":\"%s\"}",
             s_relays[idx].id, (int)s_relays[idx].gpio, s_relays[idx].state ? "ON" : "OFF");

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_relays[idx].status_topic, payload, 0, 0, 0);
    ESP_LOGI(TAG, "Published Relay %d status to [%s] (msg_id=%d): %s",
             s_relays[idx].id, s_relays[idx].status_topic, msg_id, payload);
}

static void set_relay_state(int idx, bool new_state, bool publish);

static void relay1_auto_off_callback(void *arg)
{
    ESP_LOGI(TAG, "[Auto-OFF] Timer (1 min) expired for Relay 1. Turning OFF Relay 1.");
    if (s_relays[0].state) {
        set_relay_state(0, false, true);
    }
}

static void set_relay_state(int idx, bool new_state, bool publish)
{
    if (idx < 0 || idx >= NUM_RELAYS) return;

    set_relay_hardware(idx, new_state);
    ESP_LOGI(TAG, "Relay %d (GPIO %d) set to: %s",
             s_relays[idx].id, (int)s_relays[idx].gpio, new_state ? "ON" : "OFF");

    /* Automatic 1-minute OFF timer for Relay 1 (idx == 0) */
    if (idx == 0 && s_relay1_auto_off_timer != NULL) {
        esp_timer_stop(s_relay1_auto_off_timer);
        if (new_state) {
            ESP_LOGI(TAG, "Relay 1 ON: Scheduled automatic OFF after 1 minute");
            esp_timer_start_once(s_relay1_auto_off_timer, RELAY1_AUTO_OFF_TIMEOUT_US);
        }
    }

    if (publish) {
        publish_relay_status(idx);
    }
}

/* -------------------------------------------------------------------------- */
/* Wi-Fi Event Handler                                                        */
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
            ESP_LOGW(TAG, "Retry connecting to AP (%d/%d)...", s_retry_num, CONFIG_ESP_WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi AP");
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
}

/* -------------------------------------------------------------------------- */
/* MQTT Event Handler                                                         */
/* -------------------------------------------------------------------------- */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED to broker: %s", CONFIG_MQTT_BROKER_URI);
        s_mqtt_connected = true;

        /* Subscribe to individual relay command topics */
        for (int i = 0; i < NUM_RELAYS; i++) {
            int msg_id = esp_mqtt_client_subscribe(s_mqtt_client, s_relays[i].set_topic, 0);
            ESP_LOGI(TAG, "Subscribed to [%s] (msg_id=%d)", s_relays[i].set_topic, msg_id);
            publish_relay_status(i);
        }

        /* Subscribe to bulk 'all' topic */
        int msg_all = esp_mqtt_client_subscribe(s_mqtt_client, s_all_set_topic, 0);
        ESP_LOGI(TAG, "Subscribed to bulk topic [%s] (msg_id=%d)", s_all_set_topic, msg_all);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA received from topic: %.*s", (int)event->topic_len, event->topic);
        ESP_LOGI(TAG, "Payload (%d bytes): %.*s", (int)event->data_len, (int)event->data_len, event->data);

        /* Parse command string */
        char command[16] = {0};
        int len = event->data_len < sizeof(command) - 1 ? event->data_len : sizeof(command) - 1;
        memcpy(command, event->data, len);
        command[len] = '\0';

        bool turn_on = (strcasecmp(command, "ON") == 0 || strcmp(command, "1") == 0 || strcasecmp(command, "TRUE") == 0);
        bool turn_off = (strcasecmp(command, "OFF") == 0 || strcmp(command, "0") == 0 || strcasecmp(command, "FALSE") == 0);
        bool toggle = (strcasecmp(command, "TOGGLE") == 0);

        if (!turn_on && !turn_off && !toggle) {
            ESP_LOGW(TAG, "Unknown Relay command received: '%s'. (Supported: ON, OFF, TOGGLE)", command);
            break;
        }

        /* Check if bulk command for all relays */
        if (event->topic_len == strlen(s_all_set_topic) &&
            strncmp(event->topic, s_all_set_topic, event->topic_len) == 0) {
            ESP_LOGI(TAG, "Executing bulk command '%s' on ALL 4 relays", command);
            for (int i = 0; i < NUM_RELAYS; i++) {
                bool target_state = toggle ? !s_relays[i].state : turn_on;
                set_relay_state(i, target_state, true);
                vTaskDelay(pdMS_TO_TICKS(50)); /* Stagger relay switching to prevent power supply current spikes */
            }
            break;
        }

        /* Check matching individual relay topics */
        for (int i = 0; i < NUM_RELAYS; i++) {
            if (event->topic_len == strlen(s_relays[i].set_topic) &&
                strncmp(event->topic, s_relays[i].set_topic, event->topic_len) == 0) {
                bool target_state = toggle ? !s_relays[i].state : turn_on;
                set_relay_state(i, target_state, true);
                break;
            }
        }
        break;

    default:
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
    char heartbeat_payload[384];

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
                     "{\"status\":\"online\",\"uptime_sec\":%" PRIu32 ",\"free_heap\":%" PRIu32 ",\"wifi_rssi\":%d,"
                     "\"relays\":{"
                     "\"r1\":{\"gpio\":%d,\"state\":\"%s\"},"
                     "\"r2\":{\"gpio\":%d,\"state\":\"%s\"},"
                     "\"r3\":{\"gpio\":%d,\"state\":\"%s\"},"
                     "\"r4\":{\"gpio\":%d,\"state\":\"%s\"}"
                     "}}",
                     uptime_sec, free_heap, (int)rssi,
                     (int)s_relays[0].gpio, s_relays[0].state ? "ON" : "OFF",
                     (int)s_relays[1].gpio, s_relays[1].state ? "ON" : "OFF",
                     (int)s_relays[2].gpio, s_relays[2].state ? "ON" : "OFF",
                     (int)s_relays[3].gpio, s_relays[3].state ? "ON" : "OFF");

            int msg_id = esp_mqtt_client_publish(s_mqtt_client, s_heartbeat_topic, heartbeat_payload, 0, 0, 0);
            ESP_LOGI(TAG, "Sent Heartbeat to [%s] (msg_id=%d): %s", s_heartbeat_topic, msg_id, heartbeat_payload);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Main Application Entry Point                                               */
/* -------------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "   ESP32 Wi-Fi & 4-Relay MQTT Control App  ");
    ESP_LOGI(TAG, "===========================================");

    /* Initialize Relay 1 Auto-OFF Timer */
    const esp_timer_create_args_t relay1_timer_args = {
        .callback = &relay1_auto_off_callback,
        .name = "relay1_auto_off"
    };
    ESP_ERROR_CHECK(esp_timer_create(&relay1_timer_args, &s_relay1_auto_off_timer));

    /* Initialize Relay Configuration */
    gpio_num_t relay_gpios[NUM_RELAYS] = {
        CONFIG_RELAY1_GPIO,
        CONFIG_RELAY2_GPIO,
        CONFIG_RELAY3_GPIO,
        CONFIG_RELAY4_GPIO
    };

    snprintf(s_all_set_topic, sizeof(s_all_set_topic), "%s/all/set", CONFIG_MQTT_TOPIC_BASE);
    snprintf(s_heartbeat_topic, sizeof(s_heartbeat_topic), "%s/heartbeat", CONFIG_MQTT_TOPIC_BASE);

    for (int i = 0; i < NUM_RELAYS; i++) {
        s_relays[i].id = i + 1;
        s_relays[i].gpio = relay_gpios[i];
        s_relays[i].state = false;

        snprintf(s_relays[i].set_topic, sizeof(s_relays[i].set_topic),
                 "%s/%d/set", CONFIG_MQTT_TOPIC_BASE, s_relays[i].id);
        snprintf(s_relays[i].status_topic, sizeof(s_relays[i].status_topic),
                 "%s/%d/status", CONFIG_MQTT_TOPIC_BASE, s_relays[i].id);

        /* Configure GPIO output with internal pull-up enabled for Active LOW relays */
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_relays[i].gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,       /* Keep pin HIGH on boot for Active LOW relay OFF state */
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);

        /* Set initial state OFF (Active LOW logic: OFF = HIGH, ON = LOW) */
        set_relay_hardware(i, false);
        ESP_LOGI(TAG, "Relay %d initialized on GPIO %d (Active LOW: ON=0, OFF=1)", s_relays[i].id, (int)s_relays[i].gpio);
    }

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize Wi-Fi */
    wifi_init_sta();

    /* Wait for Wi-Fi connection */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", CONFIG_ESP_WIFI_SSID);

        /* Start MQTT Client */
        mqtt_app_start();

        /* Start Heartbeat Task */
        xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 5, NULL);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", CONFIG_ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

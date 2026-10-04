#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "BLINK_APP";

/* Default GPIO pin for onboard LED on most ESP32 DevKit boards is GPIO 2 */
#define BLINK_GPIO GPIO_NUM_2
#define BLINK_DELAY_MS 1000

static uint8_t s_led_state = 0;

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configuring GPIO %d as output LED...", BLINK_GPIO);
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

void app_main(void)
{
    /* Initialize the GPIO LED pin */
    configure_led();

    ESP_LOGI(TAG, "Starting LED blink loop (interval: %d ms)...", BLINK_DELAY_MS);

    while (1) {
        ESP_LOGI(TAG, "Turning the LED %s!", s_led_state ? "ON" : "OFF");
        
        /* Set the GPIO level (0 = LOW, 1 = HIGH) */
        gpio_set_level(BLINK_GPIO, s_led_state);
        
        /* Toggle LED state for next iteration */
        s_led_state = !s_led_state;
        
        /* Delay task execution for 1000 ms */
        vTaskDelay(pdMS_TO_TICKS(BLINK_DELAY_MS));
    }
}

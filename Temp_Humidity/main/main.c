#include "app_config.h"
#include "sensor_dht11.h"
#include "status_led.h"
#include "time_sync.h"
#include "web_server.h"
#include "wifi_manager.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "app";

_Static_assert(APP_SENSOR_READ_PERIOD_MS > 2000, "DHT11 reads must be more than two seconds apart");
_Static_assert(APP_DHT11_POWER_UP_DELAY_MS >= 1000, "DHT11 needs one second to power up");

static void init_nvs(void)
{
    esp_err_t result = nvs_flash_init();

    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS recovery will erase saved settings (%s). Wi-Fi setup will be required.",
                 esp_err_to_name(result));
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }

    ESP_ERROR_CHECK(result);
}

void app_main(void)
{
    init_nvs();

    ESP_ERROR_CHECK(status_led_init(APP_STATUS_LED_GPIO));
    ESP_ERROR_CHECK(dht11_init(APP_DHT11_GPIO));

    ESP_LOGI(TAG, "Starting Wi-Fi");
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(time_sync_start());
    ESP_ERROR_CHECK(web_server_start());

    ESP_LOGI(TAG, "Waiting for DHT11 sensor to stabilise");
    vTaskDelay(pdMS_TO_TICKS(APP_DHT11_POWER_UP_DELAY_MS));

    while (1) {
        dht11_reading_t reading = {0};

        status_led_set(true);
        esp_err_t result = dht11_read(&reading);
        TickType_t read_completed = xTaskGetTickCount();
        vTaskDelay(pdMS_TO_TICKS(APP_READ_BLINK_HOLD_MS));
        status_led_set(false);

        web_server_update_reading(&reading, result);

        if (result == ESP_OK) {
            ESP_LOGI(TAG,
                     "Temperature: %.1f C / %.1f F, Humidity: %.1f%%",
                     reading.temperature_c,
                     reading.temperature_f,
                     reading.humidity_percent);
        } else {
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(result));
        }

        // Anchor each wait to this attempt, including failed reads. The extra tick
        // covers completion just before a tick boundary.
        xTaskDelayUntil(&read_completed, pdMS_TO_TICKS(APP_SENSOR_READ_PERIOD_MS) + 1);
    }
}

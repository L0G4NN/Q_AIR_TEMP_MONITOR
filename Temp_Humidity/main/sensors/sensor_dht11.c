#include "sensor_dht11.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define DHT11_START_SIGNAL_MS 20
#define DHT11_RESPONSE_TIMEOUT_US 120
#define DHT11_BIT_TIMEOUT_US 120
#define DHT11_ONE_THRESHOLD_US 50
#define DHT11_BITS 40
#define DHT11_BYTES 5

typedef enum {
    DHT11_CAPTURE_RESPONSE_LOW,
    DHT11_CAPTURE_RESPONSE_HIGH,
    DHT11_CAPTURE_BIT_LOW,
    DHT11_CAPTURE_BIT_HIGH,
    DHT11_CAPTURE_COMPLETE,
} dht11_capture_phase_t;

typedef struct {
    dht11_capture_phase_t phase;
    int bit;
} dht11_capture_status_t;

static const char *TAG = "dht11";
static gpio_num_t s_sensor_gpio = GPIO_NUM_NC;
static bool s_initialized = false;
static portMUX_TYPE s_read_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t wait_for_level(int level, uint32_t timeout_us, uint32_t *duration_us)
{
    int64_t wait_start_us = esp_timer_get_time();

    while (gpio_get_level(s_sensor_gpio) != level) {
        if ((esp_timer_get_time() - wait_start_us) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    int64_t level_start_us = esp_timer_get_time();
    while (gpio_get_level(s_sensor_gpio) == level) {
        if ((esp_timer_get_time() - level_start_us) > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (duration_us != NULL) {
        *duration_us = (uint32_t)(esp_timer_get_time() - level_start_us);
    }

    return ESP_OK;
}

static esp_err_t capture_frame(uint8_t data[DHT11_BYTES], dht11_capture_status_t *status)
{
    status->phase = DHT11_CAPTURE_RESPONSE_LOW;
    status->bit = -1;
    esp_err_t result = wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US, NULL);
    if (result != ESP_OK) {
        return result;
    }

    status->phase = DHT11_CAPTURE_RESPONSE_HIGH;
    result = wait_for_level(1, DHT11_RESPONSE_TIMEOUT_US, NULL);
    if (result != ESP_OK) {
        return result;
    }

    for (int bit = 0; bit < DHT11_BITS; bit++) {
        uint32_t high_duration_us = 0;

        status->phase = DHT11_CAPTURE_BIT_LOW;
        status->bit = bit;
        result = wait_for_level(0, DHT11_BIT_TIMEOUT_US, NULL);
        if (result != ESP_OK) {
            return result;
        }

        status->phase = DHT11_CAPTURE_BIT_HIGH;
        result = wait_for_level(1, DHT11_BIT_TIMEOUT_US, &high_duration_us);
        if (result != ESP_OK) {
            return result;
        }

        data[bit / 8] <<= 1;
        if (high_duration_us > DHT11_ONE_THRESHOLD_US) {
            data[bit / 8] |= 1;
        }
    }

    status->phase = DHT11_CAPTURE_COMPLETE;
    return ESP_OK;
}

static void log_capture_failure(const dht11_capture_status_t *status, esp_err_t result)
{
    if (result != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "DHT11 frame capture failed: %s", esp_err_to_name(result));
        return;
    }

    switch (status->phase) {
    case DHT11_CAPTURE_RESPONSE_LOW:
        ESP_LOGW(TAG, "DHT11 timeout waiting for the sensor response");
        break;
    case DHT11_CAPTURE_RESPONSE_HIGH:
        ESP_LOGW(TAG, "DHT11 timeout during the response pulse");
        break;
    case DHT11_CAPTURE_BIT_LOW:
        ESP_LOGW(TAG, "DHT11 timeout during the low pulse before bit %d", status->bit);
        break;
    case DHT11_CAPTURE_BIT_HIGH:
        ESP_LOGW(TAG, "DHT11 timeout during the high pulse for bit %d", status->bit);
        break;
    case DHT11_CAPTURE_COMPLETE:
        break;
    }
}

esp_err_t dht11_init(gpio_num_t gpio_num)
{
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio_num), ESP_ERR_INVALID_ARG, TAG, "invalid DHT11 GPIO");

    s_sensor_gpio = gpio_num;

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << s_sensor_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "failed to configure DHT11 GPIO");
    s_initialized = true;

    return ESP_OK;
}

esp_err_t dht11_read(dht11_reading_t *reading)
{
    ESP_RETURN_ON_FALSE(reading != NULL, ESP_ERR_INVALID_ARG, TAG, "reading is NULL");
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "DHT11 sensor not initialized");

    uint8_t data[DHT11_BYTES];
    memset(data, 0, sizeof(data));
    dht11_capture_status_t capture_status = {
        .phase = DHT11_CAPTURE_RESPONSE_LOW,
        .bit = -1,
    };

    ESP_RETURN_ON_ERROR(gpio_set_level(s_sensor_gpio, 0), TAG, "failed to drive DHT11 data low");
    ESP_RETURN_ON_ERROR(gpio_set_direction(s_sensor_gpio, GPIO_MODE_OUTPUT_OD),
                        TAG,
                        "failed to start DHT11 signal");
    esp_rom_delay_us(DHT11_START_SIGNAL_MS * 1000);

    // Release the open-drain bus and keep scheduling from stretching the 26-70 us pulses.
    taskENTER_CRITICAL(&s_read_lock);
    esp_err_t result = gpio_set_direction(s_sensor_gpio, GPIO_MODE_INPUT);
    if (result == ESP_OK) {
        result = capture_frame(data, &capture_status);
    }
    taskEXIT_CRITICAL(&s_read_lock);

    // Always release the single-wire bus, including after a timeout.
    esp_err_t release_result = gpio_set_direction(s_sensor_gpio, GPIO_MODE_INPUT);
    if (result != ESP_OK) {
        log_capture_failure(&capture_status, result);
        return result;
    }
    ESP_RETURN_ON_ERROR(release_result, TAG, "failed to release DHT11 data line");

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    ESP_RETURN_ON_FALSE(checksum == data[4], ESP_ERR_INVALID_CRC, TAG, "DHT11 checksum failed");
    ESP_RETURN_ON_FALSE(data[0] <= 100 && data[1] <= 9 && data[2] <= 60 && data[3] <= 9,
                        ESP_ERR_INVALID_RESPONSE,
                        TAG,
                        "DHT11 returned an out-of-range value");

    reading->humidity_percent = (float)data[0] + ((float)data[1] / 10.0f);
    reading->temperature_c = (float)data[2] + ((float)data[3] / 10.0f);
    reading->temperature_f = (reading->temperature_c * 9.0f / 5.0f) + 32.0f;

    return ESP_OK;
}

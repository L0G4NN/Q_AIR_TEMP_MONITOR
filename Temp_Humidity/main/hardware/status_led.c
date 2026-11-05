#include "status_led.h"

#include "freertos/FreeRTOS.h"

static gpio_num_t s_led_gpio = GPIO_NUM_NC;
static bool s_blink_enabled = true;
static portMUX_TYPE s_led_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t status_led_init(gpio_num_t gpio_num)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&config);
    if (result == ESP_OK) {
        result = gpio_set_level(gpio_num, 0);
    }
    if (result == ESP_OK) {
        s_led_gpio = gpio_num;
    }

    return result;
}

void status_led_set(bool on)
{
    taskENTER_CRITICAL(&s_led_lock);
    if (s_led_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_led_gpio, on && s_blink_enabled ? 1 : 0);
    }
    taskEXIT_CRITICAL(&s_led_lock);
}

void status_led_set_blink_enabled(bool enabled)
{
    taskENTER_CRITICAL(&s_led_lock);
    s_blink_enabled = enabled;
    if (!enabled && s_led_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_led_gpio, 0);
    }
    taskEXIT_CRITICAL(&s_led_lock);
}

bool status_led_is_blink_enabled(void)
{
    taskENTER_CRITICAL(&s_led_lock);
    bool enabled = s_blink_enabled;
    taskEXIT_CRITICAL(&s_led_lock);
    return enabled;
}

bool status_led_toggle_blink_enabled(void)
{
    taskENTER_CRITICAL(&s_led_lock);
    bool enabled = !s_blink_enabled;
    s_blink_enabled = enabled;
    if (!enabled && s_led_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_led_gpio, 0);
    }
    taskEXIT_CRITICAL(&s_led_lock);
    return enabled;
}

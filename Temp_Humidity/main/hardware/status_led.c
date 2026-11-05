#include "status_led.h"

static gpio_num_t s_led_gpio = GPIO_NUM_NC;
static volatile bool s_blink_enabled = true;

esp_err_t status_led_init(gpio_num_t gpio_num)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_led_gpio = gpio_num;

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << s_led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&config);
    if (result == ESP_OK) {
        gpio_set_level(s_led_gpio, 0);
    }

    return result;
}

void status_led_set(bool on)
{
    if (s_led_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_led_gpio, on && s_blink_enabled ? 1 : 0);
    }
}

void status_led_set_blink_enabled(bool enabled)
{
    s_blink_enabled = enabled;
    if (!enabled && s_led_gpio != GPIO_NUM_NC) {
        gpio_set_level(s_led_gpio, 0);
    }
}

bool status_led_is_blink_enabled(void)
{
    return s_blink_enabled;
}

bool status_led_toggle_blink_enabled(void)
{
    bool enabled = !s_blink_enabled;
    status_led_set_blink_enabled(enabled);
    return enabled;
}

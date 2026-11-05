#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t status_led_init(gpio_num_t gpio_num);
void status_led_set(bool on);
void status_led_set_blink_enabled(bool enabled);
bool status_led_is_blink_enabled(void);
bool status_led_toggle_blink_enabled(void);

#ifdef __cplusplus
}
#endif

#endif

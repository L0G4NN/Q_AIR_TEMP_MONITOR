#ifndef SENSOR_DHT11_H
#define SENSOR_DHT11_H

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature_c;
    float temperature_f;
    float humidity_percent;
} dht11_reading_t;

esp_err_t dht11_init(gpio_num_t gpio_num);
esp_err_t dht11_read(dht11_reading_t *reading);

#ifdef __cplusplus
}
#endif

#endif

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdint.h>

#include "esp_err.h"
#include "sensor_dht11.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);
void web_server_update_reading(const dht11_reading_t *reading, esp_err_t result);

#ifdef __cplusplus
}
#endif

#endif

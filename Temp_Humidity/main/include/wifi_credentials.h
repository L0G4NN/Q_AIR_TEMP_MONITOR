#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[65];
} wifi_credentials_t;

bool wifi_credentials_valid(const wifi_credentials_t *credentials);
esp_err_t wifi_credentials_load(wifi_credentials_t *credentials);
esp_err_t wifi_credentials_save(const wifi_credentials_t *credentials);
esp_err_t wifi_credentials_clear(void);

#endif

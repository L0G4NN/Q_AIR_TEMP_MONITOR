#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include "wifi_credentials.h"

typedef struct {
    const char *message;
    char address[16];
    bool busy;
    bool saved;
} wifi_setup_status_t;

esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_submit(const wifi_credentials_t *credentials);
esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count);
wifi_setup_status_t wifi_manager_setup_status(void);
bool wifi_manager_is_setup_address(uint32_t address);

#endif

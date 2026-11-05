#include "time_sync.h"

#include <stdlib.h>
#include <time.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time";
static bool s_started;
static bool s_sntp_initialized;
static esp_event_handler_instance_t s_ip_event_instance;

static esp_err_t initialize_sntp(void)
{
    if (s_sntp_initialized) {
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_NTP_SERVER);
    esp_err_t result = esp_netif_sntp_init(&config);
    if (result == ESP_OK) {
        s_sntp_initialized = true;
        ESP_LOGI(TAG, "Synchronizing time from %s", APP_NTP_SERVER);
    }
    return result;
}

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    esp_err_t result = initialize_sntp();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start time synchronization: %s", esp_err_to_name(result));
    }
}

esp_err_t time_sync_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    if (setenv("TZ", APP_TIMEZONE, 1) != 0) {
        return ESP_FAIL;
    }
    tzset();

    esp_err_t result = esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &got_ip_handler,
                                                            NULL,
                                                            &s_ip_event_instance);
    if (result != ESP_OK) {
        return result;
    }

    s_started = true;

    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (station != NULL && esp_netif_get_ip_info(station, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        return initialize_sntp();
    }

    return ESP_OK;
}

bool time_sync_get_epoch(time_t *epoch_seconds)
{
    if (epoch_seconds == NULL) {
        return false;
    }

    time_t now;
    time(&now);

    struct tm utc_time;
    if (gmtime_r(&now, &utc_time) == NULL || utc_time.tm_year < (2024 - 1900)) {
        return false;
    }

    *epoch_seconds = now;
    return true;
}

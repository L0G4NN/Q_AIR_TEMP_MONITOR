#include "wifi_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "secrets.h"

static const char *TAG = "wifi";
static bool s_started;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to begin Wi-Fi connection: %s", esp_err_to_name(result));
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u); reconnecting", (unsigned int)event->reason);

        esp_err_t result = esp_wifi_connect();
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Failed to restart Wi-Fi connection: %s", esp_err_to_name(result));
        }
        return;
    }

}

esp_err_t wifi_manager_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    const size_t ssid_length = strlen(WIFI_SSID);
    const size_t password_length = strlen(WIFI_PASSWORD);

    ESP_RETURN_ON_FALSE(ssid_length > 0 && ssid_length <= sizeof(((wifi_config_t *)0)->sta.ssid),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "WIFI_SSID must contain 1-32 characters");
    ESP_RETURN_ON_FALSE(password_length <= sizeof(((wifi_config_t *)0)->sta.password),
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "WIFI_PASSWORD must contain at most 64 characters");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize TCP/IP stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create default event loop");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "failed to create station network interface");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "failed to initialize Wi-Fi");

    esp_event_handler_instance_t wifi_instance;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &wifi_instance),
                        TAG,
                        "failed to register Wi-Fi handler");

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, WIFI_SSID, ssid_length);
    memcpy(wifi_config.sta.password, WIFI_PASSWORD, password_length);
    wifi_config.sta.threshold.authmode = password_length == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID \"%s\"", WIFI_SSID);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "failed to set station config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    s_started = true;

    return ESP_OK;
}

#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bootloader_random.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#define SECOND_US INT64_C(1000000)
#define RETRY_DELAY (5 * SECOND_US)
#define SETUP_TIMEOUT (30 * SECOND_US)
#define SETUP_SUCCESS_GRACE (30 * SECOND_US)
#define SETUP_MAX_HANDOFF (120 * SECOND_US)
#define MODE_RETRY_DELAY SECOND_US

typedef enum {
    NORMAL,
    SETUP_STARTING,
    SETUP_READY,
    SETUP_TESTING,
    SETUP_RESET_STA,
    SETUP_STOPPING_STA,
    SETUP_ENABLE_STA,
    SETUP_SAVED,
} wifi_state_t;

static const char *TAG = "wifi";
static SemaphoreHandle_t s_lock;
static esp_netif_t *s_station;
static esp_netif_t *s_setup_ap;
/* All mutable state below is protected by s_lock after startup. */
static wifi_state_t s_state;
static wifi_credentials_t s_candidate;
static const char *s_message = "Choose the Wi-Fi network for your monitor.";
static char s_address[16];
static bool s_scanning;
static int64_t s_deadline;
static int64_t s_retry_at;
static int64_t s_mode_retry_at;
static int64_t s_handoff_limit;

static void log_setup_network(void)
{
    wifi_config_t ap;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap) == ESP_OK) {
        ESP_LOGI(TAG, "Join %s with setup password %s, then open http://192.168.4.1/",
                 (const char *)ap.ap.ssid, (const char *)ap.ap.password);
    }
}

static esp_err_t configure_station(const wifi_credentials_t *credentials)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, strlen(credentials->ssid));
    memcpy(config.sta.password, credentials->password, strlen(credentials->password));
    config.sta.threshold.authmode = credentials->password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_STA, &config);
}

/* Wait for STA_STOP before enabling it again so the default handler can
 * tear down the previous attempt's DHCP client. Keep the setup AP running. */
static void setup_failed(const char *message)
{
    s_message = message;
    memset(&s_candidate, 0, sizeof(s_candidate));
    s_state = SETUP_RESET_STA;
    s_mode_retry_at = esp_timer_get_time();
    s_deadline = s_mode_retry_at + SETUP_TIMEOUT;
}

static bool change_mode(wifi_mode_t mode, wifi_state_t next_state)
{
    esp_err_t result = esp_wifi_set_mode(mode);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Could not change Wi-Fi mode to %d: %s", (int)mode, esp_err_to_name(result));
        return false;
    }
    s_state = next_state;
    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        log_setup_network();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_state == NORMAL) {
            s_retry_at = esp_timer_get_time();
        } else if (s_state == SETUP_STARTING) {
            s_state = SETUP_READY;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP && s_state == SETUP_STOPPING_STA) {
        s_state = SETUP_ENABLE_STA;
        s_mode_retry_at = esp_timer_get_time();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "Station disconnected (reason %u)", (unsigned)event->reason);
        if (s_state == SETUP_TESTING) {
            setup_failed("Could not connect. Check the network name, password and signal, then try again.");
        } else if (s_state == NORMAL || s_state == SETUP_SAVED) {
            s_address[0] = '\0';
            s_retry_at = esp_timer_get_time() + RETRY_DELAY;
            if (s_state == SETUP_SAVED) {
                s_deadline = 0;
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        esp_netif_ip_info_t current;
        if (event->esp_netif == s_station && event->ip_info.ip.addr != 0 &&
            (s_state == SETUP_TESTING || s_state == SETUP_SAVED) &&
            esp_netif_is_netif_up(s_station) &&
            esp_netif_get_ip_info(s_station, &current) == ESP_OK && current.ip.addr == event->ip_info.ip.addr) {
            if (s_state == SETUP_TESTING) {
                esp_err_t result = wifi_credentials_save(&s_candidate);
                memset(&s_candidate, 0, sizeof(s_candidate));
                if (result == ESP_OK) {
                    s_state = SETUP_SAVED;
                    s_handoff_limit = esp_timer_get_time() + SETUP_MAX_HANDOFF;
                    s_mode_retry_at = 0;
                } else if (result == ESP_ERR_NVS_REMOVE_FAILED) {
                    ESP_LOGW(TAG, "Previous Wi-Fi settings could not be removed; new settings may apply on restart");
                    setup_failed("Could not confirm the save. The new settings may apply after a restart. "
                                 "Try saving again, or restart and check the connection.");
                } else {
                    ESP_LOGE(TAG, "Could not save Wi-Fi settings: %s", esp_err_to_name(result));
                    setup_failed("Connected, but could not save settings. Try again.");
                }
            }
            if (s_state == SETUP_SAVED) {
                if (s_address[0] == '\0') {
                    s_deadline = esp_timer_get_time() + SETUP_SUCCESS_GRACE;
                }
                snprintf(s_address, sizeof(s_address), IPSTR, IP2STR(&event->ip_info.ip));
            }
        }
    }

    xSemaphoreGive(s_lock);
}

static void handle_setup_button(int64_t held)
{
    if (held >= 15 * SECOND_US) {
        esp_err_t result = wifi_credentials_clear();
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "Saved Wi-Fi settings cleared. Restarting in setup mode.");
            esp_restart();
        }
        ESP_LOGE(TAG, "Could not clear Wi-Fi settings: %s", esp_err_to_name(result));
    } else if (held >= 5 * SECOND_US && s_state == NORMAL) {
        if (change_mode(WIFI_MODE_AP, SETUP_STOPPING_STA)) {
            s_message = "Choose the Wi-Fi network for your monitor.";
            s_address[0] = '\0';
            s_retry_at = 0;
            s_deadline = esp_timer_get_time() + SETUP_TIMEOUT;
        }
    }
}

static void maintain_setup(int64_t now)
{
    switch (s_state) {
    case SETUP_TESTING:
        if (now >= s_deadline) {
            setup_failed("Timed out waiting for an IP address. Check your router and try again.");
        }
        break;

    case SETUP_RESET_STA:
    case SETUP_STOPPING_STA:
    case SETUP_ENABLE_STA:
    case SETUP_STARTING:
        if (now >= s_deadline) {
            ESP_LOGE(TAG, "Could not recover the setup station. Restarting the monitor.");
            esp_restart();
        } else if (now >= s_mode_retry_at) {
            if (s_state == SETUP_RESET_STA) {
                change_mode(WIFI_MODE_AP, SETUP_STOPPING_STA);
            } else if (s_state == SETUP_ENABLE_STA) {
                change_mode(WIFI_MODE_APSTA, SETUP_STARTING);
            }
            s_mode_retry_at = now + MODE_RETRY_DELAY;
        }
        break;

    case SETUP_SAVED:
        if ((now >= s_handoff_limit || (s_deadline != 0 && now >= s_deadline)) &&
            now >= s_mode_retry_at && !change_mode(WIFI_MODE_STA, NORMAL)) {
            if (now >= s_handoff_limit + SETUP_TIMEOUT) {
                ESP_LOGE(TAG, "Could not stop the setup hotspot. Restarting the monitor.");
                esp_restart();
            }
            s_mode_retry_at = now + MODE_RETRY_DELAY;
        }
        break;

    default:
        break;
    }
}

static void wifi_maintenance(void *arg)
{
    (void)arg;
    int64_t pressed_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time();

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (gpio_get_level(APP_WIFI_SETUP_GPIO) == 0) {
            if (pressed_at == 0) {
                pressed_at = now;
            }
        } else if (pressed_at != 0) {
            handle_setup_button(now - pressed_at);
            pressed_at = 0;
        }

        maintain_setup(now);
        if ((s_state == NORMAL || s_state == SETUP_SAVED) && s_retry_at != 0 && now >= s_retry_at) {
            esp_err_t result = esp_wifi_connect();
            s_retry_at = result == ESP_OK ? 0 : now + RETRY_DELAY;
        }
        xSemaphoreGive(s_lock);
    }
}

esp_err_t wifi_manager_start(void)
{
    wifi_credentials_t saved = {0};
    esp_err_t result = wifi_credentials_load(&saved);
    if (result == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Saved Wi-Fi settings are invalid. Starting setup.");
    } else if (result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(result, TAG, "failed to read Wi-Fi settings");
    }
    s_state = result == ESP_OK ? NORMAL : SETUP_STARTING;

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << APP_WIFI_SETUP_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button), TAG, "failed to configure setup button");
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate Wi-Fi mutex");

    uint8_t random[8];
    bootloader_random_enable();
    esp_fill_random(random, sizeof(random));
    bootloader_random_disable();

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "failed to read hotspot MAC");
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "Q-Air-Setup-%02X%02X", mac[4], mac[5]);
    for (size_t i = 0; i < sizeof(random); i++) {
        snprintf((char *)ap.ap.password + 2 * i, 3, "%02x", random[i]);
    }
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize TCP/IP stack");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create default event loop");
    s_station = esp_netif_create_default_wifi_sta();
    s_setup_ap = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_station && s_setup_ap, ESP_ERR_NO_MEM, TAG, "failed to create Wi-Fi interfaces");

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "failed to select RAM storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "failed to enable Wi-Fi interfaces");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "failed to configure setup hotspot");
    if (s_state == NORMAL) {
        ESP_RETURN_ON_ERROR(configure_station(&saved), TAG, "failed to configure saved network");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to select station mode");
    }
    memset(&saved, 0, sizeof(saved));

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
                        TAG, "failed to register Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
                        TAG, "failed to register IP events");
    s_deadline = esp_timer_get_time() + SETUP_TIMEOUT;
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    ESP_RETURN_ON_FALSE(xTaskCreate(wifi_maintenance, "wifi_maintenance", 4096, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "failed to start Wi-Fi maintenance");
    return ESP_OK;
}

esp_err_t wifi_manager_submit(const wifi_credentials_t *credentials)
{
    if (!wifi_credentials_valid(credentials)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != SETUP_READY || s_scanning) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = configure_station(credentials);
    if (result == ESP_OK) {
        s_candidate = *credentials;
        s_state = SETUP_TESTING;
        s_deadline = esp_timer_get_time() + SETUP_TIMEOUT;
        s_message = "Testing Wi-Fi and waiting for an IP address…";
        result = esp_wifi_connect();
        if (result != ESP_OK) {
            setup_failed("Could not start the connection. Try again.");
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_state != SETUP_READY || s_scanning) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_scanning = true;
    xSemaphoreGive(s_lock);

    esp_err_t result = esp_wifi_scan_start(NULL, true);
    if (result == ESP_OK) {
        result = esp_wifi_scan_get_ap_records(count, records);
        if (result != ESP_OK) {
            esp_wifi_clear_ap_list();
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scanning = false;
    xSemaphoreGive(s_lock);
    return result;
}

wifi_setup_status_t wifi_manager_setup_status(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    wifi_setup_status_t status = {
        .message = s_message,
        .busy = s_scanning || (s_state != SETUP_READY && s_state != SETUP_SAVED),
        .saved = s_state == SETUP_SAVED,
    };
    if (status.saved) {
        status.message = s_address[0]
            ? "Wi-Fi saved. Rejoin your normal Wi-Fi and open the dashboard address below."
            : "Wi-Fi saved. Waiting for the monitor to reconnect and receive a dashboard address.";
    }
    memcpy(status.address, s_address, sizeof(status.address));
    xSemaphoreGive(s_lock);
    return status;
}

bool wifi_manager_is_setup_address(uint32_t address)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_netif_ip_info_t ap_ip;
    esp_netif_ip_info_t station_ip;
    bool setup = s_state != NORMAL && address != 0 &&
                 esp_netif_get_ip_info(s_setup_ap, &ap_ip) == ESP_OK && address == ap_ip.ip.addr &&
                 (esp_netif_get_ip_info(s_station, &station_ip) != ESP_OK || address != station_ip.ip.addr);
    xSemaphoreGive(s_lock);
    return setup;
}

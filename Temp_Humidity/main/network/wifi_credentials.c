#include "wifi_credentials.h"

#include <string.h>
#include "nvs.h"

#define WIFI_NAMESPACE "qair_wifi"
#define WIFI_KEY "credentials"

bool wifi_credentials_valid(const wifi_credentials_t *credentials)
{
    size_t ssid_length = strnlen(credentials->ssid, sizeof(credentials->ssid));
    size_t password_length = strnlen(credentials->password, sizeof(credentials->password));
    if (ssid_length == 0 || ssid_length == sizeof(credentials->ssid)) {
        return false;
    }
    if (password_length == 64) {
        return strspn(credentials->password, "0123456789abcdefABCDEF") == 64;
    }
    return password_length == 0 || (password_length >= 8 && password_length <= 63);
}

esp_err_t wifi_credentials_load(wifi_credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }

    size_t size = sizeof(*credentials);
    result = nvs_get_blob(handle, WIFI_KEY, credentials, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_INVALID_LENGTH || result == ESP_ERR_NVS_TYPE_MISMATCH ||
        (result == ESP_OK && (size != sizeof(*credentials) || !wifi_credentials_valid(credentials)))) {
        return ESP_ERR_INVALID_ARG;
    }
    return result;
}

esp_err_t wifi_credentials_save(const wifi_credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_set_blob(handle, WIFI_KEY, credentials, sizeof(*credentials));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t wifi_credentials_clear(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    result = nvs_erase_key(handle, WIFI_KEY);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : result;
}

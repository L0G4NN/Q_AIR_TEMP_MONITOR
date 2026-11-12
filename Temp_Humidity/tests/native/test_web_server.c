#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../main/network/web_server.c"

static int64_t now_us;
static char response[200000];
static size_t response_length;
static bool close_requested;
static bool blink;
static unsigned mutations;

int64_t esp_timer_get_time(void) { return now_us; }
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned timeout) { (void)timeout; return semaphore != NULL; }
int xSemaphoreGive(SemaphoreHandle_t semaphore) { return semaphore != NULL; }
const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "TEST_ERROR"; }
bool time_sync_get_epoch(time_t *epoch) { *epoch = 0; return false; }
bool status_led_is_blink_enabled(void) { return blink; }
bool status_led_toggle_blink_enabled(void) { mutations++; return blink = !blink; }
bool wifi_setup_request(httpd_req_t *req) { (void)req; return false; }
esp_err_t wifi_setup_send_page(httpd_req_t *req) { (void)req; return ESP_FAIL; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) { (void)req; (void)type; return ESP_OK; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value)
{
    (void)req;
    if (!strcmp(name, "Connection") && !strcmp(value, "close")) close_requested = true;
    return ESP_OK;
}
esp_err_t httpd_resp_send(httpd_req_t *req, const char *data, long length)
{
    (void)req;
    size_t size = length < 0 ? strlen(data) : (size_t)length;
    assert(response_length + size < sizeof(response));
    memcpy(response + response_length, data, size);
    response_length += size;
    response[response_length] = 0;
    return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *data)
{
    return httpd_resp_send(req, data, data ? (long)strlen(data) : 0);
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, long length)
{
    return httpd_resp_send(req, data, length);
}
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *req, const char *data)
{
    return httpd_resp_sendstr(req, data);
}
esp_err_t httpd_resp_send_err(httpd_req_t *req, int error, const char *message)
{
    assert(error == HTTPD_400_BAD_REQUEST);
    return httpd_resp_sendstr(req, message);
}

static void sample(int seconds, float temperature, esp_err_t result)
{
    now_us = (int64_t)seconds * 1000000;
    dht11_reading_t reading = {temperature, temperature * 1.8f + 32, 50};
    web_server_update_reading(&reading, result);
}

int main(void)
{
    s_state_mutex = &now_us;
    sample(1, -0.1f, ESP_OK);
    sample(31, -0.3f, ESP_OK);
    sample(61, 0, ESP_ERR_TIMEOUT); /* flush average of the first minute */
    assert(s_history_count == 1 && s_history[0].temperature_tenths_c == -2);
    sample(121, 20, ESP_OK); /* no value for the empty interval */
    sample(181, 21, ESP_OK);
    assert(s_history_count == 2 && s_history[1].seconds_since_boot == 181);

    for (unsigned i = 0; i < APP_HISTORY_CAPACITY + 3; i++) {
        append_history_sample(300 + i * 60, -0.1f, 50);
    }
    assert(s_history_count == APP_HISTORY_CAPACITY);
    httpd_req_t request = {0};
    assert(send_history_json(&request) == ESP_OK);
    assert(strstr(response, "\"temperature_c\":-0.1,\"humidity_percent\":50.0"));
    char first[64];
    snprintf(first, sizeof(first), "\"samples\":[{\"seconds_since_boot\":%u", 300 + 3 * 60);
    assert(strstr(response, first));
    assert(response[response_length - 1] == '}');

    request.content_len = 1000000;
    close_requested = false;
    assert(toggle_blink(&request) == ESP_FAIL && close_requested && mutations == 0);
    assert(send_history_json(&request) == ESP_FAIL);
    assert(send_home_page(&request) == ESP_FAIL);
    assert(send_reading_json(&request) == ESP_FAIL);
    puts("PASS: minute averages, gaps, ring rollover, negative JSON tenths, and body rejection before mutation");
    return 0;
}

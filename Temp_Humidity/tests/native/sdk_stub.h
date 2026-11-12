#ifndef TEST_SDK_STUB_H
#define TEST_SDK_STUB_H

/* Declarations for host unit tests. Unused hardware paths are removed by the
 * linker; these tests do not emulate GPIO timing, FreeRTOS or the radio. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef int esp_err_t;
enum {
    ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT,
    ESP_ERR_INVALID_CRC, ESP_ERR_INVALID_RESPONSE, ESP_ERR_NO_MEM,
    ESP_FAIL = -1,
};
#define ESP_RETURN_ON_FALSE(value, error, ...) do { if (!(value)) return (error); } while (0)
#define ESP_RETURN_ON_ERROR(value, ...) do { esp_err_t result_ = (value); if (result_ != ESP_OK) return result_; } while (0)
static inline void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI test_log
#define ESP_LOGW test_log
#define ESP_LOGE test_log
const char *esp_err_to_name(esp_err_t error);

typedef int gpio_num_t;
enum {
    GPIO_NUM_NC = -1, GPIO_NUM_0, GPIO_NUM_2 = 2, GPIO_NUM_4 = 4,
    GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_OUTPUT_OD,
    GPIO_PULLUP_DISABLE, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, GPIO_INTR_DISABLE,
};
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) ((gpio) >= 0 && (gpio) < 34)
typedef struct {
    uint64_t pin_bit_mask;
    int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio, int level);
esp_err_t gpio_set_direction(gpio_num_t gpio, int direction);
int gpio_get_level(gpio_num_t gpio);
int64_t esp_timer_get_time(void);

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
void vTaskDelay(unsigned ticks);
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned timeout);
int xSemaphoreGive(SemaphoreHandle_t semaphore);

typedef int esp_event_base_t;
typedef void *esp_event_handler_instance_t;
enum { IP_EVENT, IP_EVENT_STA_GOT_IP };
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int event,
    void (*handler)(void *, esp_event_base_t, int32_t, void *),
    void *argument, esp_event_handler_instance_t *instance);

typedef int esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip; } esp_netif_ip_info_t;
typedef struct { esp_netif_t *esp_netif; esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) ((ip)->addr & 255), (((ip)->addr >> 8) & 255), (((ip)->addr >> 16) & 255), ((ip)->addr >> 24)
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);

typedef struct { size_t content_len; } httpd_req_t;
typedef void *httpd_handle_t;
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
    void *user_ctx;
} httpd_uri_t;
typedef struct { bool lru_purge_enable; } httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_RESP_USE_STRLEN (-1)
enum { HTTP_GET, HTTP_POST, HTTPD_400_BAD_REQUEST, HTTPD_500_INTERNAL_SERVER_ERROR };
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t server);
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *req, const char *data, long length);
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *data);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req, const char *data, long length);
esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *req, const char *data);
esp_err_t httpd_resp_send_err(httpd_req_t *req, int error, const char *message);
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route);
#endif

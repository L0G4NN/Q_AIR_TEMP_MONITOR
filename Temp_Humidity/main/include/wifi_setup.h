#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_http_server.h"

bool wifi_setup_request(httpd_req_t *req);
esp_err_t wifi_setup_send_page(httpd_req_t *req);
esp_err_t wifi_setup_register_routes(httpd_handle_t server);

#endif

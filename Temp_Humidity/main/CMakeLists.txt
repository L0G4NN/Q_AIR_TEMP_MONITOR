idf_component_register(SRCS "main.c"
                            "hardware/status_led.c"
                            "network/web_server.c"
                            "network/timesync.c"
                            "network/wifi_manager.c"
                            "sensors/sensor_dht11.c"
                       REQUIRES esp_driver_gpio esp_event esp_http_server esp_netif esp_timer esp_wifi esp_rom nvs_flash
                       INCLUDE_DIRS "include")

idf_component_register(SRCS "main.c"
                            "hardware/status_led.c"
                            "network/web_server.c"
                            "network/timesync.c"
                            "network/wifi_manager.c"
                            "network/wifi_credentials.c"
                            "network/wifi_setup.c"
                            "sensors/sensor_dht11.c"
                       REQUIRES bootloader_support esp_hw_support esp_driver_gpio esp_event esp_http_server esp_netif esp_timer esp_wifi esp_rom esp_system lwip nvs_flash
                       INCLUDE_DIRS "include")

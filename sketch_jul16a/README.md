# Legacy Arduino reference

This sketch provides a small Wi-Fi temperature/humidity webpage. The maintained ESP-IDF firmware in [`Temp_Humidity`](../Temp_Humidity/README.md) includes first-time browser setup, trend history and BOOT recovery.

To build this reference, use the Espressif ESP32 Arduino core and install Adafruit's **DHT sensor library** and its **Adafruit Unified Sensor** dependency. Select the board and serial port for your ESP32. The sketch uses DHT11 data on GPIO 4.

Set the SSID and password placeholders locally before uploading. These values are compiled into the Arduino firmware and must not be committed or shared. This reference does not use the ESP-IDF browser setup or its saved NVS credentials.

The initial Wi-Fi wait ends after ten seconds so sensor sampling can continue if the router is unavailable. The loop requests a reconnect every 30 seconds while disconnected and prints the dashboard address when it connects. Sampling retains the last good values and labels them as stale after a failed read.

HTTP header parsing uses a two-second elapsed-time budget and a 1,024-byte limit. Elapsed-time subtraction preserves the deadline across `millis()` rollover. Headers are read byte by byte to avoid a dynamic `String` allocation per line or a separate blocking line-read timeout.

Run the repository's [host checks](../CONTRIBUTING.md#required-checks) to exercise the offline, request-bound and stale-reading paths. Those tests use Wi-Fi/DHT doubles and do not replace compiling with your installed Arduino core or checking a physical board.

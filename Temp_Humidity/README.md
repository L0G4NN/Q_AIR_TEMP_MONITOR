# Temp Humidity Firmware

This folder contains the ESP-IDF firmware for the Q Air Temp Monitor desk temperature sensor.

The firmware reads a DHT11 temperature/humidity sensor every 2.5 seconds, pulses a status LED each time a reading is attempted, connects to Wi-Fi, and serves the latest values from an ESP-IDF HTTP server. The automatic read blink can be enabled or disabled from the button beside the webpage title. Successful readings are averaged into one-minute trend samples. A 1,440-point RAM ring buffer retains the latest populated minutes: approximately 24 hours with continuous readings, potentially longer when intervals are missing. SNTP synchronizes the ESP32 clock after Wi-Fi connects so the title bar can show the current date and time and the graph can display local clock times along its x-axis. Sensor sampling continues while Wi-Fi is disconnected, and the station reconnects automatically.

## File Layout

- `main/main.c` - application startup and periodic sensor read loop
- `main/include/app_config.h` - GPIO pins and timing
- `main/sensors/sensor_dht11.c` - DHT11 GPIO protocol reader
- `main/hardware/status_led.c` - read-activity LED pulse
- `main/network/wifi_manager.c` - setup hotspot, station connection, and recovery button
- `main/network/wifi_credentials.c` - Wi-Fi credential storage in NVS
- `main/network/wifi_setup.c` - browser setup page and endpoints
- `main/network/timesync.c` - SNTP clock synchronization
- `main/network/web_server.c` - browser page and JSON endpoint

## Hardware Defaults

- DHT11 data pin: `GPIO_NUM_4`
- Status LED pin: `GPIO_NUM_2`
- Wi-Fi setup button: `GPIO_NUM_0` with pull-up (BOOT on typical ESP32 dev boards)
- Sensor power-up delay: 1000 ms
- Sensor read period: 2500 ms
- LED pulse hold time: 60 ms
- Trend sample interval: 60 seconds
- Trend capacity: 1440 populated minute intervals (approximately 24 hours with continuous readings)
- Time server: `pool.ntp.org`
- Firmware timezone: UK GMT/BST; browser labels use the browser's local timezone

Change these values in `main/include/app_config.h` if your board uses different pins. The read period must remain above 2000 ms for a DHT11.

Connect the DHT11 data pin to GPIO 4 and add a 4.7-10 kOhm pull-up resistor from data to 3.3 V. DHT11 modules often include this resistor; a bare sensor usually does not. Power the sensor from 3.3 V and connect its ground to ESP32 ground.

## Wi-Fi Setup

1. Flash with `idf.py -p COM_PORT flash monitor`. If no settings are saved, the ESP32 prints the setup network name and its randomly generated WPA2 password.
2. Join that network and visit [http://192.168.4.1/](http://192.168.4.1/) in a browser. Choose **Scan for networks** or enter the SSID manually, including for hidden networks. Use a 2.4 GHz personal Wi-Fi network, or leave the password blank for an open network.
3. Select **Test and save Wi-Fi**. Setup stays available during the test. If your phone drops the hotspot while the ESP32 changes Wi-Fi channels, rejoin it and reload the page.
4. Once the ESP32 receives an IP address, it saves the settings and displays the dashboard address. If the station disconnects before the hotspot shuts down, the page waits for a new address. The hotspot normally closes after 30 seconds with an address, or after two minutes if the connection keeps dropping. Rejoin your usual Wi-Fi and open the dashboard address. You can also find it in the serial monitor.

Settings survive a restart or a firmware flash that preserves NVS. They are stored in the `qair_wifi` namespace, separate from Wi-Fi driver storage. The default build does not encrypt NVS. Upgrading from the old `secrets.h` firmware requires setup once, since that build kept the station configuration in RAM. Keep any old firmware images that contain compiled-in credentials private.

- **Change Wi-Fi:** while the firmware is running, hold the setup button for 5 to under 15 seconds and release. Follow the same setup steps. A failed connection test keeps the last saved network. If a flash error leaves the save unconfirmed, the page warns that the new settings may apply after a restart. Power-cycle the device to cancel an unfinished reconfiguration.
- **Clear Wi-Fi:** hold the setup button for at least 15 seconds and release. This clears the saved Wi-Fi settings and restarts in setup mode.
- **Router offline:** the monitor continues sampling and retries every five seconds after a disconnect. The setup hotspot stays off until you request it.

If your board lacks a BOOT button on GPIO 0, configure `APP_WIFI_SETUP_GPIO` for a suitable input and use a momentary switch to ground. Press BOOT only while the firmware is running. Holding it during a reset can enter the ESP32 bootloader.

## Web Page

After Wi-Fi setup, open the IP address printed in the serial monitor:

```text
I web: Web server available at http://192.168.x.x
```

Routes:

- `/` - HTML page showing the synchronized date and time, live sensor readings, a title-bar blink toggle, sensor status, and an interactive 24-hour temperature/humidity trend graph
- `/api/reading` - JSON version of the latest reading and blink state; `valid` describes the latest attempt and `stale` indicates that retained values came from an earlier successful read
- `/api/history` - streamed JSON containing the retained one-minute trend samples plus the synchronized Unix-time/uptime reference used for graph labels
- `POST /api/blink/toggle` - toggles the automatic status-LED pulse and returns its new state

Trend history is stored in RAM and starts empty after the ESP32 restarts. Minutes without a successful DHT11 reading are omitted instead of recording a misleading value; the graph breaks lines across missing intervals. Until SNTP succeeds, the graph temporarily uses elapsed-time labels and retries the clock reference every five seconds. Live values refresh every five seconds after the preceding request completes; synchronized history refreshes every minute. Blink selection also resets on reboot.

Hover over the graph with a mouse, or tap it on a touchscreen, to select the nearest one-minute sample. The graph displays its time, temperature, and humidity with a guide line and highlighted markers.

## Build

Activate an ESP-IDF 6.0 terminal first, then run `idf.py build` from this directory. The local installation uses the ESP-IDF Python environment and Xtensa toolchain; an ordinary Python terminal is insufficient.

The checked-in configuration currently targets the original ESP32. Use a different `set-target` value only if the board uses another ESP32 family chip.

## Flash

```text
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the board serial port.

## Reliability and resource use

- Sensor start pulses yield to FreeRTOS rather than busy-waiting for 20 ms. The short microsecond frame capture remains protected from scheduling interruptions.
- The next read is scheduled from the completion of each sensor attempt so delayed wake-ups preserve the configured minimum interval.
- Checksum and range checks reject corrupt frames, including humidity above 100% and temperatures outside -20 to 60 C. The DHT11 V1.3 sign bit is decoded before checking the temperature range. Failed reads retain the last good dashboard values.
- LED toggle state and GPIO updates share a lock across the HTTP and sampling tasks.
- SNTP initializes once before registering its IP handler and restarts synchronization on IP acquisition.
- History samples occupy 8 bytes each (11,520 bytes for the ring). An HTTP request snapshots populated samples before transmission, temporarily allocating up to another 11,520 bytes. Copies use at most two contiguous memory operations; JSON records are batched into HTTP chunks.
- History JSON is formatted directly from stored integer tenths, including negative values, avoiding repeated floating-point conversion for up to 1,440 records. Formatting checks response bounds.
- Both browser pages bound requests to eight seconds and resume polling after a timeout. Setup status polling prevents overlapping status requests.
- The dashboard and setup APIs close requests with unexpected bodies before HTTPD can drain those bodies and hold up other clients. Credential uploads retain their five-second total body-read budget.

The DHT11 has coarse measurement resolution. Averaged decimal values do not imply more precise hardware measurements. History does not persist across resets.

The [manufacturer's DHT11 V1.3 datasheet](https://www.aosong.com/userfiles/files/media/DHT11-V1_3%E8%AF%B4%E6%98%8E%E4%B9%A6%EF%BC%88%E8%AF%A6%E7%BB%86%E7%89%88%EF%BC%89.pdf) specifies a signed temperature format and an 18-30 ms start pulse. The nominal 20 ms start pulse yields to FreeRTOS, so scheduling delays can extend it. Verify that pulse and the short protected capture interval on the actual board.

If NVS initialisation finds no free pages or a newer incompatible NVS format, the existing recovery path erases the NVS partition and logs a warning. Saved Wi-Fi settings must then be entered again. Clearing a setting removes it logically; it does not promise secure physical erasure of flash.

## Verification

Build with ESP-IDF 6.0 from this directory.

```text
idf.py build
```

From the repository root, run the browser and native regression suites.

```text
node --test Temp_Humidity/tests/test_web_ui.js Temp_Humidity/tests/test_wifi_setup_ui.js
python3 Temp_Humidity/tests/run_native.py
```

The browser suite executes the actual JavaScript embedded in the firmware. It checks sequential polling and request timeouts, raw SSIDs, the saved-address handoff, failed blink toggles, graph gaps and isolated readings.

The native suite compiles the actual sensor and web-server source using small SDK test doubles and GCC/Clang on Linux. It checks signed sensor frames and boundaries, checksum failures, minute averages and missing intervals, full-ring rollover, negative history JSON, and request rejection before mutation. It also compiles and exercises the legacy Arduino sketch with Wi-Fi/DHT test doubles. Set `CC` or `CXX` to select another compiler. Tests build in a temporary directory.

Host tests do not measure hardware pulse timing, Wi-Fi association, DHCP or flash reliability. After flashing, check initial setup, a wrong password followed by a correct one, a router outage, channel changes, both BOOT actions, sensor disconnection and a known-temperature reading. Espressif QEMU can boot the firmware but does not emulate the ESP32 Wi-Fi radio.

The setup AP and station receiving exactly the same IP address remains a deferred edge case. If it prevents setup completion, use the serial monitor to inspect the connection and reconfigure the network.

The HTTP dashboard is intended for a trusted local network. It has no application authentication or TLS. Capture any history you need before flashing because restarting clears the RAM buffer.

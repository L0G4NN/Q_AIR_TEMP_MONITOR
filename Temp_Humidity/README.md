# Temp Humidity Firmware

This folder contains the ESP-IDF firmware for the Q Air Temp Monitor desk temperature sensor.

The firmware reads a DHT11 temperature/humidity sensor every 2.5 seconds, pulses a status LED each time a reading is attempted, connects to Wi-Fi, and serves the latest values from an ESP-IDF HTTP server. The automatic read blink can be enabled or disabled from the button beside the webpage title. Successful readings are averaged into one-minute trend samples. A 1,440-point RAM ring buffer retains up to 24 hours of temperature and humidity history. SNTP synchronizes the ESP32 clock after Wi-Fi connects so the title bar can show the current date and time and the graph can display local clock times along its x-axis. Sensor sampling continues while Wi-Fi is disconnected, and the station reconnects automatically.

## File Layout

- `main/main.c` - application startup and periodic sensor read loop
- `main/include/app_config.h` - GPIO pins and timing
- `main/include/secrets.h` - local Wi-Fi SSID and password
- `main/include/secrets.example.h` - shareable credentials template
- `main/sensors/sensor_dht11.c` - DHT11 GPIO protocol reader
- `main/hardware/status_led.c` - read-activity LED pulse
- `main/network/wifi_manager.c` - Wi-Fi station connection
- `main/network/timesync.c` - SNTP clock synchronization
- `main/network/web_server.c` - browser page and JSON endpoint

## Hardware Defaults

- DHT11 data pin: `GPIO_NUM_4`
- Status LED pin: `GPIO_NUM_2`
- Sensor power-up delay: 1000 ms
- Sensor read period: 2500 ms
- LED pulse hold time: 60 ms
- Trend sample interval: 60 seconds
- Trend capacity: 1440 samples (24 hours)
- Time server: `pool.ntp.org`
- Timezone: UK GMT/BST

Change these values in `main/include/app_config.h` if your board uses different pins. The read period must remain above 2000 ms for a DHT11.

Connect the DHT11 data pin to GPIO 4 and add a 4.7-10 kOhm pull-up resistor from data to 3.3 V. DHT11 modules often include this resistor; a bare sensor usually does not. Power the sensor from 3.3 V and connect its ground to ESP32 ground.

## Wi-Fi Setup

If `main/include/secrets.h` is missing, copy `main/include/secrets.example.h` to that name. Then edit `secrets.h` before building:

```c
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

`secrets.h` is ignored by this ESP-IDF project's `.gitignore`. Keep `secrets.example.h` as the template if you publish the project.

## Web Page

After flashing, open the IP address printed in the serial monitor:

```text
I web: Web server available at http://192.168.x.x
```

Routes:

- `/` - HTML page showing the synchronized date and time, live sensor readings, a title-bar blink toggle, sensor status, and an interactive 24-hour temperature/humidity trend graph
- `/api/reading` - JSON version of the latest reading and blink state; `valid` describes the latest attempt and `stale` indicates that retained values came from an earlier successful read
- `/api/history` - streamed JSON containing the retained one-minute trend samples plus the synchronized Unix-time/uptime reference used for graph labels
- `POST /api/blink/toggle` - toggles the automatic status-LED pulse and returns its new state

Trend history is stored in RAM and starts empty after the ESP32 restarts. Minutes without a successful DHT11 reading are omitted instead of recording a misleading value. Until SNTP succeeds, the graph temporarily uses elapsed-time labels and retries the clock reference every five seconds.

Hover over the graph with a mouse, or tap it on a touchscreen, to select the nearest one-minute sample. The graph displays its time, temperature, and humidity with a guide line and highlighted markers.

## Build

```text
idf.py set-target esp32
idf.py build
```

The checked-in configuration currently targets the original ESP32. Use a different `set-target` value only if the board uses another ESP32 family chip.

## Flash

```text
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the board serial port.

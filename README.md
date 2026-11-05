# Q Air Temp Monitor

Q Air Temp Monitor is a desk temperature and humidity sensor project built with ESP-IDF. The firmware reads a DHT11 sensor every 2.5 seconds, blinks a status LED for each read attempt, connects to Wi-Fi, and serves the latest readings on a local web page.

## Project Goal

The device sits on a desk, reads local temperature and humidity data, connects to Wi-Fi, and displays the readings in a browser.

## Current Firmware

- ESP-IDF project location: `Temp_Humidity`
- Sensor: DHT11 on `GPIO_NUM_4`
- Status LED: `GPIO_NUM_2`
- Read period: 2500 ms
- Web routes: `/` and `/api/reading`
- Wi-Fi credentials: `Temp_Humidity/main/include/secrets.h`
- Arduino sketch location: `sketch_jul16a`
- Sketch status: older Arduino DHT11 temperature and humidity reader with a Wi-Fi web page

The ESP-IDF project is the main firmware. The Arduino sketch is kept as a simple reference.

## Arduino DHT11 Sketch

The `sketch_jul16a/sketch_jul16a.ino` sketch reads a DHT11 sensor and serves the latest temperature and humidity readings on a simple web page.

Before uploading, update these values in the sketch:

- `DHT_PIN` if the DHT11 data line is not connected to GPIO 4.
- `ssid` and `password` with the WiFi network details.

## ESP-IDF Setup

Before building the ESP-IDF project, edit:

```text
Temp_Humidity/main/include/secrets.h
```

If that ignored local file is missing, create it by copying `secrets.example.h` from the same directory.

Set:

```c
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

Change GPIO defaults in `Temp_Humidity/main/include/app_config.h` if needed.

## Build

From the firmware directory:

```text
cd Temp_Humidity
idf.py set-target esp32
idf.py build
```

The checked-in configuration targets the original ESP32. Change the target only if the board uses a different ESP32 family chip.

## Flash

```text
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with the serial port for the connected board.

# Q Air Temp Monitor

An ESP32 desk temperature and humidity monitor using a DHT11 and ESP-IDF 6.0. The device samples every 2.5 seconds and serves a local browser dashboard with live readings, a clock, an LED blink toggle, and an interactive temperature/humidity graph.

## Firmware and hardware

The maintained firmware is in [`Temp_Humidity`](Temp_Humidity/README.md). The [legacy Arduino reference](sketch_jul16a/README.md) is in `sketch_jul16a`.

| Setting | Default |
| --- | --- |
| Target | Original ESP32 |
| DHT11 data | GPIO 4 |
| Status LED | GPIO 2, active high |
| Wi-Fi setup button | GPIO 0, active low (BOOT on typical ESP32 dev boards) |
| Sensor interval | 2.5 seconds |
| History | 1,440 one-minute averages in RAM |
| Time synchronization | SNTP using `pool.ntp.org` |

Power the DHT11 from 3.3 V, share ground with the ESP32, and use a 4.7-10 kOhm pull-up from data to 3.3 V. Sensor modules often include the pull-up. Pins and timing are configured in `Temp_Humidity/main/include/app_config.h`.

## Setup and build

Use an ESP-IDF 6.0 terminal with its Python environment and toolchain activated. From the repository root:

```text
cd Temp_Humidity
idf.py build
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with your board's port. The checked-in configuration targets the original ESP32.

1. On first boot, join the `Q-Air-Setup-XXXX` Wi-Fi network using the setup password printed in the serial monitor. If your phone flags the hotspot as having no internet, stay connected to it.
2. Open [http://192.168.4.1/](http://192.168.4.1/), choose or enter your 2.4 GHz network, and select **Test and save Wi-Fi**.
3. When setup shows the dashboard address, rejoin your normal Wi-Fi and open that address. The serial monitor also prints it.

To change networks, hold **BOOT** for at least 5 seconds and release it while the monitor is running. To clear its saved Wi-Fi settings, hold **BOOT** for at least 15 seconds and release it. See the [firmware README](Temp_Humidity/README.md#wifi-setup) for details.

## Dashboard and API

| Route | Purpose |
| --- | --- |
| `GET /` | Dashboard, clock, graph, and blink control |
| `GET /api/reading` | Latest attempt status, last good values, read/failure counters, and blink state |
| `GET /api/history` | Minute averages with uptime and clock reference |
| `POST /api/blink/toggle` | Toggle the automatic read LED pulse |

Failed sensor reads retain the last good value and mark it stale. History averages successful reads only and omits empty intervals. The ring holds the latest 1,440 populated minutes: approximately 24 hours with continuous readings, potentially longer when readings are missing. History and blink selection reset on reboot. DHT11 output is coarse; decimal formatting and averaging do not increase the sensor's physical resolution. The decoder supports the signed temperature format used by DHT11 V1.3 units and rejects values outside -20 to 60 C.

Sampling continues through Wi-Fi outages. Wi-Fi reconnects automatically, and SNTP restarts after an IP address is acquired. The browser displays synchronized timestamps in its own local timezone.

Wi-Fi credentials are entered after flashing and stored in the ESP32's NVS partition. Firmware builds do not need `secrets.h`. An ESP32 upgraded from the old firmware needs to be set up once.

See the [firmware README](Temp_Humidity/README.md) for implementation details and verification commands.

## Development and checks

Use the long-lived `develop` branch for follow-up work and open pull requests into `main`. The [contribution guide](CONTRIBUTING.md) covers branch synchronisation, build checks and hardware verification.

The development container defaults to ESP-IDF `v6.0`. Open `Temp_Humidity` as the VS Code workspace, select your installed ESP-IDF tools and serial port, and build once to generate the compilation database.

From the repository root, run the dependency-free browser tests and native host tests.

```text
node --test Temp_Humidity/tests/test_web_ui.js Temp_Humidity/tests/test_wifi_setup_ui.js
python3 Temp_Humidity/tests/run_native.py
```

Native tests need Python 3 and a GCC/Clang C/C++ toolchain on Linux. They cover the sensor decoder, history aggregation/formatting, request-body rejection, and the Arduino reference using test doubles. The ESP-IDF target build and physical-board checks remain separate.

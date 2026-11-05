# Q Air Temp Monitor

An ESP32 desk temperature and humidity monitor using a DHT11 and ESP-IDF 6.0. The device samples every 2.5 seconds and serves a local browser dashboard with live readings, a clock, an LED blink toggle, and an interactive temperature/humidity graph.

## Firmware and hardware

The maintained firmware is in [`Temp_Humidity`](Temp_Humidity/README.md). The older Arduino reference is in `sketch_jul16a`.

| Setting | Default |
| --- | --- |
| Target | Original ESP32 |
| DHT11 data | GPIO 4 |
| Status LED | GPIO 2, active high |
| Sensor interval | 2.5 seconds |
| History | 1,440 one-minute averages in RAM |
| Time synchronization | SNTP using `pool.ntp.org` |

Power the DHT11 from 3.3 V, share ground with the ESP32, and use a 4.7-10 kOhm pull-up from data to 3.3 V. Sensor modules often include the pull-up. Pins and timing are configured in `Temp_Humidity/main/include/app_config.h`.

## Setup and build

Use an ESP-IDF 6.0 terminal with its Python environment and toolchain activated. From the repository root:

```powershell
Copy-Item Temp_Humidity/main/include/secrets.example.h Temp_Humidity/main/include/secrets.h
```

Run the copy only for a fresh checkout; edit your existing `secrets.h` if it is already present. Enter your Wi-Fi name and password there, then build:

```text
cd Temp_Humidity
idf.py build
idf.py -p COM_PORT flash monitor
```

Replace `COM_PORT` with your board's port. The checked-in `sdkconfig` targets `esp32`. For another ESP32 family chip, run `idf.py set-target YOUR_TARGET` before building and check the GPIO assignments. After flashing, open the local IP address printed in the serial monitor.

## Dashboard and API

| Route | Purpose |
| --- | --- |
| `GET /` | Dashboard, clock, graph, and blink control |
| `GET /api/reading` | Latest attempt status, last good values, read/failure counters, and blink state |
| `GET /api/history` | Minute averages with uptime and clock reference |
| `POST /api/blink/toggle` | Toggle the automatic read LED pulse |

Failed sensor reads retain the last good value and mark it stale. History averages successful reads only and omits empty intervals. The ring holds the latest 1,440 populated minutes: approximately 24 hours with continuous readings, potentially longer when readings are missing. History and blink selection reset on reboot. DHT11 output is coarse; decimal formatting and averaging do not increase the sensor's physical resolution.

Sampling continues through Wi-Fi outages. Wi-Fi reconnects automatically, and SNTP restarts after an IP address is acquired. The browser displays synchronized timestamps in its own local timezone.

## Credentials

`secrets.h`, build output, and local agent settings are ignored by Git. Only the placeholder `secrets.example.h` should be shared. Wi-Fi configuration uses RAM storage to avoid writing another copy to NVS; this does not erase configuration stored by older firmware. Credentials are still compiled into firmware binaries, so keep generated binaries private.

See the [firmware README](Temp_Humidity/README.md) for implementation details and verification commands.

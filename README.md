# ESP32 C1001 Offline Vital Monitor

Standalone ESP32 firmware and offline web UI for a non-contact single-person breathing and heart-rate monitor using the DFRobot `C1001` 60 GHz mmWave sensor.

## Features

- ESP32 runs as a local Wi-Fi access point
- Offline web server with no CDN or cloud dependency
- WebSocket live streaming for current sensor readings
- On-device history buffer and browser chart
- CSV log persistence in `LittleFS`
- Log and history download buttons in the dashboard
- Uses the official `DFRobot_HumanDetection` library instead of reverse-parsing UART frames

## Project Layout

- `esp32_c1001_monitor.ino`
- `data/index.html`
- `data/styles.css`
- `data/app.js`

## Wiring

Typical ESP32 wiring for the module:

- `C1001 VIN` -> `ESP32 5V`
- `C1001 GND` -> `ESP32 GND`
- `C1001 TX` -> `ESP32 GPIO16`
- `C1001 RX` -> `ESP32 GPIO17`

Important:

- The `C1001` uses `5V` power.
- On `NodeMCU-32S`, this project uses `GPIO16/GPIO17` at `115200` baud for the sensor UART.
- For respiration rate and heart rate, DFRobot recommends placing the sensor within about `1.5m` and facing the chest.

## Arduino IDE Setup

Open this sketch folder directly in Arduino IDE:

- `F:\GOOGLE Antigravity\BCA\esp32_c1001_monitor`

The folder name and sketch name already match, which Arduino IDE requires.

### 1. Install ESP32 board support

In Arduino IDE:

- `File -> Preferences`
- Add this Board Manager URL if needed:
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
- Open `Tools -> Board -> Boards Manager`
- Install `esp32 by Espressif Systems`

### 2. Install required libraries

In `Tools -> Manage Libraries`, install:

- `ArduinoJson` by Benoit Blanchon
- `WebSockets` by Markus Sattler
- `DFRobot_HumanDetection` by DFRobot

`WiFi`, `WebServer`, and `LittleFS` come from the ESP32 core.

### 3. Select board settings

Typical working settings:

- `Board`: `ESP32 Dev Module`
- `Flash Size`: at least `4MB`
- `Partition Scheme`: choose one with `LittleFS` or enough app/data space
- `Port`: your ESP32 serial port

### 4. Install a filesystem uploader

To upload the `data/` folder you need an Arduino IDE filesystem upload tool that supports ESP32 `LittleFS`.

Use one of these approaches:

- Arduino IDE 2.x plugin/tool for ESP32 LittleFS upload
- legacy ESP32 Sketch Data Upload tool if you are on Arduino IDE 1.8.x

The required behavior is the same: upload the contents of the local `data/` folder into ESP32 `LittleFS`.

### 5. Upload firmware and web assets

1. Open `esp32_c1001_monitor.ino`
2. Click `Verify`
3. Click `Upload`
4. Run the ESP32 `LittleFS` data upload tool for the `data/` folder
5. Open `Serial Monitor` at `115200`

## Runtime

After boot, the ESP32 creates:

- SSID: `C1001-Monitor`
- Password: `radar12345`

Open:

- `http://192.168.4.1/`

The dashboard shows:

- live breath rate
- live heart rate
- movement score
- presence estimate
- sensor read/error stats
- recent history chart
- CSV downloads

## Log Format

The persisted log file is `LittleFS:/vitals.csv`.

Columns:

- `millis`
- `uptime`
- `breath_bpm`
- `heart_bpm`
- `motion_score`
- `present`
- `movement_state`
- `confidence`

Because this system is intentionally offline, timestamps are based on device uptime unless you add an RTC or another trusted time source.

## Notes

- This build follows DFRobot's documented approach of using `DFRobot_HumanDetection` in `sleep mode` for presence, respiration, and heart rate on the `C1001`.
- Presence and confidence here are application-level heuristics, not medical or safety-certified signals.
- If your board uses different UART pins, update `kSensorRxPin` and `kSensorTxPin` in `esp32_c1001_monitor.ino`.
- If the dashboard says `Filesystem unavailable`, upload the `data/` folder to `LittleFS` again instead of formatting automatically.

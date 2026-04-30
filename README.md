Here’s a **cleaned, tighter, and more professional version** of your README (reduced verbosity, better flow, still complete but thesis-friendly):

---

# ESP32 C1001 Offline Vital Monitor

Standalone ESP32-based IoT system for **non-contact breathing and heart-rate monitoring** using the DFRobot `C1001` 60 GHz mmWave sensor. Designed for **privacy-first operation** with fully offline processing and local web interface.

---

## Key Features

* Local Wi-Fi access point (no internet required)
* Fully offline web dashboard (no cloud/CDN dependency)
* Real-time data via WebSocket streaming
* On-device history + browser-based visualization
* CSV data logging using LittleFS
* Downloadable logs and history
* Uses official `DFRobot_HumanDetection` library

---

## Project Structure

```
esp32_c1001_monitor.ino
data/
 ├── index.html
 ├── styles.css
 └── app.js
```

---

## Hardware Wiring

| C1001 Pin | ESP32  |
| --------- | ------ |
| VIN       | 5V     |
| GND       | GND    |
| TX        | GPIO16 |
| RX        | GPIO17 |

**Notes:**

* Requires **5V power supply**
* UART runs at **115200 baud**
* Optimal sensing distance: **≤ 1.5m (chest-facing)**

---

## Arduino IDE Setup

### 1. Install ESP32 Board

* Preferences → Add URL:
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
* Install: *esp32 by Espressif Systems*

### 2. Required Libraries

* ArduinoJson
* WebSockets
* DFRobot_HumanDetection

(Built-in: WiFi, WebServer, LittleFS)

### 3. Board Configuration

* Board: ESP32 Dev Module
* Flash: ≥ 4MB
* Partition: LittleFS supported

---

## Upload Steps

1. Open `esp32_c1001_monitor.ino`
2. Verify & Upload firmware
3. Upload `/data` folder to LittleFS
4. Open Serial Monitor (115200 baud)

---

## Runtime Access

* SSID: `C1001-Monitor`
* Password: `radar12345`
* Dashboard: `http://192.168.4.1/`

---

## Dashboard Includes

* Breathing rate
* Heart rate
* Motion detection
* Presence estimation
* Sensor statistics
* Live charts + history
* CSV export

---

## Log Format

Stored in: `LittleFS:/vitals.csv`

Fields:

* millis, uptime
* breath_bpm, heart_bpm
* motion_score
* present, movement_state
* confidence

---

## Important Notes

* Fully offline system → no real-world timestamps (uptime-based)
* Not medical-grade; values are indicative only
* Ensure correct UART pins (`GPIO16/17`) or update in code
* If UI fails, re-upload LittleFS data

---

## Summary

This project demonstrates a **privacy-focused IoT health monitoring system** using mmWave sensing, enabling **contactless vital tracking with zero cloud dependency**, suitable for edge computing and secure environments.

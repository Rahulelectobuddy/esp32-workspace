# ESP32 Development Workspace

Welcome to the ESP32 ESP-IDF development workspace. This repository contains ready-to-use ESP32 projects, setup automation scripts, and step-by-step guides for building, flashing, and monitoring ESP32 applications.

---

## 📁 Repository Structure

```
esp32-workspace/
├── env.sh                 # Environment activation script for ESP-IDF
├── create-project.sh      # Shell script to scaffold new ESP32 projects
├── README.md              # Main workspace documentation
├── 01_blink/              # Project 1: Basic GPIO LED Blinking
│   ├── CMakeLists.txt
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── main.c
│   └── README.md
├── 02_wifi_mqtt_led/      # Project 2: Wi-Fi + MQTT Remote LED Control & Telemetry
│   ├── CMakeLists.txt
│   ├── main/
│   │   ├── CMakeLists.txt
│   │   └── main.c
│   ├── sdkconfig.defaults
│   └── README.md
└── 03_wifi_mqtt_4relays/  # Project 3: Wi-Fi + 4-Relay MQTT Independent Control
    ├── CMakeLists.txt
    ├── main/
    │   ├── CMakeLists.txt
    │   └── main.c
    ├── sdkconfig.defaults
    └── README.md
```

---

## 🚀 Quick Start Guide

### 1. Activate ESP-IDF Environment

Before running any `idf.py` commands, activate the ESP-IDF environment script in your shell session:

```zsh
source env.sh
```

### 2. Create a New Project (Optional)

Use the built-in generator script to quickly scaffold a new ESP-IDF application:

```zsh
./create-project.sh <project_name>
```

---

## 🛠️ How to Build, Flash & Monitor

Follow these steps for any project directory (e.g., `01_blink`, `02_wifi_mqtt_led`, or `03_wifi_mqtt_4relays`):

### Step 1: Navigate to the Project Folder
```zsh
cd 03_wifi_mqtt_4relays
```

### Step 2: Set the Target Architecture (First Time / Chip Change)
```zsh
idf.py set-target esp32
```
*(Supported targets: `esp32`, `esp32s2`, `esp32s3`, `esp32c3`, `esp32c6`, etc.)*

### Step 3: Configure Project (Optional)
To configure Wi-Fi credentials, MQTT broker details, or system settings:
```zsh
idf.py menuconfig
```

### Step 4: Build the Project
```zsh
idf.py build
```

### Step 5: Identify Your Serial Port
Connect your ESP32 board via USB cable to your computer:

* **macOS:**
  ```zsh
  ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.usbmodem*
  ```
  *(Example detected port: `/dev/cu.usbserial-0001`)*

* **Linux:**
  ```zsh
  ls /dev/ttyUSB* /dev/ttyACM*
  ```

### Step 6: Flash to ESP32 & Monitor Output
Combine flashing and serial log monitoring into a single command:

```zsh
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

> 💡 **Tip:** Press `Ctrl + ]` to exit the serial log monitor.

---

## 📦 Projects Overview

### 1. `01_blink`
* **Description:** Simple GPIO output example that blinks the onboard LED (GPIO 2) at 1-second intervals.
* **Key Components:** `driver/gpio.h`, FreeRTOS tasks (`vTaskDelay`).
* **Flash command example:**
  ```zsh
  cd 01_blink && idf.py -p /dev/cu.usbserial-0001 flash monitor
  ```

### 2. `02_wifi_mqtt_led`
* **Description:** Full IoT application featuring Wi-Fi connection, MQTT client subscription, remote LED toggling, and periodic telemetry publication.
* **MQTT Topics:**
  * Subscribes to `esp32/led/set` (`ON`, `OFF`, `TOGGLE`)
  * Publishes status to `esp32/led/status`
  * Publishes telemetry every 10s to `esp32/heartbeat` (uptime, free heap, RSSI, LED state)
* **Testing via MQTT CLI:**
  ```zsh
  mosquitto_pub -h broker.hivemq.com -t "esp32/led/set" -m "ON"
  mosquitto_sub -h broker.hivemq.com -t "esp32/#" -v
  ```

### 3. `03_wifi_mqtt_4relays`
* **Description:** Advanced 4-channel IoT relay controller over Wi-Fi & MQTT. Features individual topics and status responses per GPIO, bulk relay control (`esp32/relay/all/set`), and multi-relay telemetry heartbeat.
* **GPIO Mapping:**
  * Relay 1: **GPIO 18** &rarr; Topics: `esp32/relay/1/set` | `esp32/relay/1/status`
  * Relay 2: **GPIO 19** &rarr; Topics: `esp32/relay/2/set` | `esp32/relay/2/status`
  * Relay 3: **GPIO 22** &rarr; Topics: `esp32/relay/3/set` | `esp32/relay/3/status`
  * Relay 4: **GPIO 23** &rarr; Topics: `esp32/relay/4/set` | `esp32/relay/4/status`
* **Bulk Control:** `esp32/relay/all/set` (`ON`, `OFF`, `TOGGLE`)
* **Testing via MQTT CLI:**
  ```zsh
  mosquitto_pub -h broker.hivemq.com -t "esp32/relay/1/set" -m "ON"
  mosquitto_pub -h broker.hivemq.com -t "esp32/relay/all/set" -m "OFF"
  mosquitto_sub -h broker.hivemq.com -t "esp32/relay/#" -v
  ```


---

## 🔧 Troubleshooting & Useful Commands

| Issue / Action | Command / Solution |
| :--- | :--- |
| **Environment not loaded** | Run `source env.sh` |
| **Serial connection timeout / upload error** | Hold the **BOOT / IO0** button on the ESP32 while flashing starts |
| **Port Permission Denied (Linux)** | `sudo usermod -a -G dialout $USER` (then re-login) |
| **Clean Build Cache** | `idf.py fullclean` |
| **Exit Serial Monitor** | Press `Ctrl + ]` |

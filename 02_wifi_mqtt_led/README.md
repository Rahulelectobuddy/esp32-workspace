# 02_wifi_mqtt_led - ESP32 Wi-Fi & MQTT LED Control with Heartbeat

An ESP-IDF project that connects to a Wi-Fi network, connects to an MQTT broker, subscribes to a command topic to control the onboard LED, reports LED status on state change, and sends periodic heartbeat & telemetry.

---

## Features

1. **Wi-Fi Connectivity:** Automatically connects to your configured Wi-Fi network.
2. **MQTT Command Control:** Subscribes to `esp32/led/set` for remote control:
   - Send `"ON"`, `"1"`, or `"true"` to turn LED ON.
   - Send `"OFF"`, `"0"`, or `"false"` to turn LED OFF.
   - Send `"TOGGLE"` to flip the LED state.
3. **LED Status Feedback:** Publishes LED state changes to `esp32/led/status` (e.g. `{"led_state":"ON"}`).
4. **Periodic Heartbeat & Telemetry:** Publishes telemetry every 10 seconds to `esp32/heartbeat`:
   ```json
   {
     "status": "online",
     "uptime_sec": 42,
     "free_heap": 218452,
     "wifi_rssi": -55,
     "led_state": "OFF"
   }
   ```

---

## Configuration

Before flashing, configure your Wi-Fi credentials and MQTT broker details using `idf.py menuconfig`:

1. Activate ESP-IDF environment:
   ```bash
   source ../env.sh
   cd 02_wifi_mqtt_led
   ```
2. Open configuration menu:
   ```bash
   idf.py menuconfig
   ```
3. Navigate to **"ESP32 Wi-Fi & MQTT Configuration"**:
   - Set **Wi-Fi SSID**
   - Set **Wi-Fi Password**
   - Set **MQTT Broker URI** (e.g., `mqtt://broker.hivemq.com` or `mqtt://192.168.1.100:1883`)

---

## How to Build & Flash

1. **Build Project:**
   ```bash
   idf.py build
   ```

2. **Flash and Monitor:**
   ```bash
   idf.py -p /dev/cu.usbserial-XXXX flash monitor
   ```

---

## Testing via MQTT Client / CLI (`mosquitto_pub` / `mosquitto_sub`)

You can test controlling the LED from any MQTT client (e.g. `mosquitto_pub`, MQTTX, Home Assistant):

- **Turn LED ON:**
  ```bash
  mosquitto_pub -h broker.hivemq.com -t "esp32/led/set" -m "ON"
  ```

- **Turn LED OFF:**
  ```bash
  mosquitto_pub -h broker.hivemq.com -t "esp32/led/set" -m "OFF"
  ```

- **Listen to Heartbeat & Status:**
  ```bash
  mosquitto_sub -h broker.hivemq.com -t "esp32/#" -v
  ```

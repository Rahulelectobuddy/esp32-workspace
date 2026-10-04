# 03_wifi_mqtt_4relays - ESP32 4-Channel Relay Control via Wi-Fi & MQTT

An ESP-IDF application that connects to Wi-Fi and an MQTT broker to independently control 4 physical relays connected to 4 GPIO pins. Supports individual control/status MQTT topics for each relay, a bulk control topic for all relays, and a 10-second telemetry heartbeat.

---

## ⚡ Default Relay GPIO Mapping

| Relay Channel | Default GPIO | Command Topic | Status Topic | Features |
| :--- | :--- | :--- | :--- | :--- |
| **Relay 1** | **GPIO 18** | `esp32/relay/1/set` | `esp32/relay/1/status` | **Auto-OFF after 1 min** |
| **Relay 2** | **GPIO 19** | `esp32/relay/2/set` | `esp32/relay/2/status` | Standard |
| **Relay 3** | **GPIO 22** | `esp32/relay/3/set` | `esp32/relay/3/status` | Standard |
| **Relay 4** | **GPIO 23** | `esp32/relay/4/set` | `esp32/relay/4/status` | Standard |
| **All Relays** | **ALL** | `esp32/relay/all/set` | *(Responds on each status topic)* | Bulk toggle |

---

## 🛰️ MQTT Payload Formats

### 1. Control Commands (`esp32/relay/<1..4>/set` or `esp32/relay/all/set`)
Send plain string commands:
* `"ON"`, `"1"`, or `"TRUE"` &rarr; Turns relay ON
* `"OFF"`, `"0"`, or `"FALSE"` &rarr; Turns relay OFF
* `"TOGGLE"` &rarr; Flips current relay state

### 2. Individual Status Response (`esp32/relay/<1..4>/status`)
Published automatically on state changes:
```json
{
  "relay": 1,
  "gpio": 18,
  "state": "ON"
}
```

### 3. Periodic Telemetry (`esp32/relay/heartbeat`)
Published every 10 seconds:
```json
{
  "status": "online",
  "uptime_sec": 60,
  "free_heap": 218940,
  "wifi_rssi": -51,
  "relays": {
    "r1": {"gpio": 18, "state": "ON"},
    "r2": {"gpio": 19, "state": "OFF"},
    "r3": {"gpio": 22, "state": "OFF"},
    "r4": {"gpio": 23, "state": "OFF"}
  }
}
```

---

## 🚀 How to Build & Flash

1. **Activate Environment & Navigate to Directory:**
   ```bash
   source ../env.sh
   cd 03_wifi_mqtt_4relays
   ```

2. **Configure Wi-Fi & MQTT (Optional):**
   ```bash
   idf.py menuconfig
   ```
   *(Navigate to "ESP32 Wi-Fi & 4-Relay MQTT Configuration" to adjust Wi-Fi credentials, MQTT Broker URI, or GPIO pins)*

3. **Set Target & Build:**
   ```bash
   idf.py set-target esp32
   idf.py build
   ```

4. **Flash & Monitor:**
   ```bash
   idf.py -p /dev/cu.usbserial-0001 flash monitor
   ```

---

## 🧪 Testing via Mosquitto CLI

* **Turn Relay 1 ON:**
  ```bash
  mosquitto_pub -h broker.hivemq.com -t "esp32/relay/1/set" -m "ON"
  ```
* **Turn Relay 2 OFF:**
  ```bash
  mosquitto_pub -h broker.hivemq.com -t "esp32/relay/2/set" -m "OFF"
  ```
* **Turn ALL Relays ON:**
  ```bash
  mosquitto_pub -h broker.hivemq.com -t "esp32/relay/all/set" -m "ON"
  ```
* **Monitor All Relay Responses & Telemetry:**
  ```bash
  mosquitto_sub -h broker.hivemq.com -t "esp32/relay/#" -v
  ```

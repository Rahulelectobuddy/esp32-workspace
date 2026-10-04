# 01_blink - ESP32 LED Blinking Example

A simple ESP-IDF application for blinking an LED connected to GPIO 2 (the standard onboard LED on ESP32 DevKit boards).

## Quick Start Instructions

1. **Activate ESP-IDF Environment:**
   ```bash
   source ../env.sh
   ```

2. **Set Target Chip (if using a different board like ESP32-S3 or ESP32-C3):**
   ```bash
   idf.py set-target esp32
   ```

3. **Build Project:**
   ```bash
   idf.py build
   ```

4. **Flash to ESP32 Board:**
   Connect your ESP32 board via USB, then run:
   ```bash
   idf.py -p /dev/cu.usbserial-XXXX flash monitor
   ```
   *(Replace `/dev/cu.usbserial-XXXX` with your board's serial port; on Mac it is typically `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`)*

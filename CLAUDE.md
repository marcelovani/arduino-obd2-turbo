# CLAUDE.md

At the start of every session, read these two files before doing anything else:

- [README.md](README.md) — project overview, hardware list, and Wokwi simulation setup
- [esp32_obd2_dashboard_plan.md](esp32_obd2_dashboard_plan.md) — full technical plan: wiring, Turbo trigger algorithm, OBD2 protocol, state machine, code skeleton, all decisions made, and references

## Project summary

This is an Arduino/ESP32 project that emulates a turbo blow-off valve (Turbo)
sound. It reads live OBD2 data (throttle, RPM, speed) via Bluetooth from an
ELM327 dongle and plays a "pssssh" MP3 sound through a DFPlayer Mini when
the driver lifts off the throttle during a gear change.

Target car: Mercedes CLA180, 2011, gasoline, manual gearbox.

## Key decisions already made

- Board: ELEGOO ESP-WROOM-32 (ESP-WROOM-32 chip, Bluetooth Classic)
- OLED: 0.96" SSD1306 (model ep0096dtan001a) — use `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`
- Audio: DFPlayer Mini on Serial2 (GPIO 16 RX, GPIO 17 TX)
- Input: KY-040 rotary encoder (GPIO 25 CLK, 26 DT, 27 SW)
- Libraries: U8g2 (not U8glib), DFRobotDFPlayerMini, Bounce2
- ELM327 is Bluetooth Classic only (not WiFi, not BLE)
- G-force displayed relative to 1g (gravity subtracted)
- Gearbox is manual — Turbo trigger uses throttle-drop detection (no clutch PID needed)
- Speaker via DFPlayer Mini's built-in 3W amp (SPK1/SPK2 pins)
- Future upgrade: Bluetooth audio to car radio via separate A2DP transmitter module (ESP32 BT is occupied by OBD2)
- Turbo triggers on throttle drop: >40% → <10%, RPM >1500, gear ≤ 2

## Coding conventions

- Follow the same class/file structure as [arduino-laser-target](https://github.com/marcelovani/arduino-laser-target)
- DFPlayer on `Serial2` (hardware serial) — not SoftwareSerial like the laser-target project
- Encoder reading: manual CLK/DT polling (no encoder library)
- Button debounce: Bounce2 library
- No FreeRTOS — cooperative scheduling with `millis()` timers

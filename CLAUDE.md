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
- OLED: 0.96" SSD1306 SPI (pins: GND,VCC,D0,D1,RES,DC,CS) — real device uses `U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI`; Wokwi simulation uses `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` (Wokwi's component is I2C-only)
- OLED SPI pins (real device): SCK=GPIO18, MOSI=GPIO23, RES=GPIO15, DC=GPIO32, CS=GPIO5
- Audio: DFPlayer Mini on Serial2 (GPIO 16 RX, GPIO 17 TX)
- Input: KY-040 rotary encoder (GPIO 25 CLK, 26 DT, 27 SW)
- Libraries: U8g2 (not U8glib), DFRobotDFPlayerMini, Bounce2
- ELM327 is Bluetooth Classic only (not WiFi, not BLE)
- G-force displayed relative to 1g (gravity subtracted)
- Gearbox is manual — Turbo trigger uses throttle-drop detection (no clutch PID needed)
- Speaker via DFPlayer Mini's built-in 3W amp (SPK1/SPK2 pins)
- DFPlayer VCC: 3.3V (from ESP32 3V3 pin) — spec is 3.2–5V so 3.3V works fine; no separate 5V supply needed
- DFPlayer RX: 1kΩ resistor in series (protective; legs at E21–F21 bridging breadboard centre gap)
- Future upgrade: Bluetooth audio to car radio via separate A2DP transmitter module (ESP32 BT is occupied by OBD2)
- Turbo triggers on throttle drop: >40% → <10%, RPM >1500, gear ≤ 2

## Wokwi simulation hardware (`#ifdef SIMULATION`)

The Wokwi sim replaces real-device peripherals with two components on GPIO:

| Component      | Pin          | Role                                                           |
| -------------- | ------------ | -------------------------------------------------------------- |
| Passive buzzer | GPIO17 (TX2) | `tone(900 Hz, 350 ms)` on every Turbo fire — replaces DFPlayer |
| Red LED        | GPIO4        | Blinks for 1 s after each Turbo fire — visual indicator        |

**Note: GPIO2 is the ESP32 DevKit built-in LED — do not use it for external circuits.**
GPIO17 (TX2) is free in simulation because DFPlayer (`PIN_DFP_TX`) is `#else`-guarded.

## Keeping Python in sync with C++

`lib/turbo_logic.py` is a manual Python mirror of two C++ files. If you change
either of the files below, update the corresponding function in `lib/turbo_logic.py`
and run `make test` to verify they agree:

| C++ file                         | Python equivalent       |
| -------------------------------- | ----------------------- |
| `sketches/turbo/GearEstimator.h` | `estimate_gear()`       |
| `sketches/turbo/TurboTrigger.h`  | `TurboTrigger.update()` |

Constants in `Config.h` are parsed automatically — no manual sync needed there.

**Future improvement:** compile the C++ logic as a shared library and call it from
Python via `ctypes` or `pybind11`. That would eliminate drift entirely — the Python
tests and viewer would run the actual C++ functions. Requires stripping Arduino-specific
globals from the `.h` files first.

## After changing sketch or diagram

After any change to `sketches/turbo/turbo.ino` or `Emulators/Wokwi/diagram.json`:

1. Run `make test` — verifies logic is correct (Python unit + integration tests)
2. Run `make wokwi-build` — recompiles the sketch so Wokwi uses the new firmware
3. Restart the Wokwi simulator in VS Code (F1 → Wokwi: Start Simulator)

Skipping step 2 means the simulator runs stale firmware — pin changes, threshold
changes, and display changes will not be visible.

## Coding conventions

- Follow the same class/file structure as [arduino-laser-target](https://github.com/marcelovani/arduino-laser-target)
- DFPlayer on `Serial2` (hardware serial) — not SoftwareSerial like the laser-target project
- Encoder reading: manual CLK/DT polling (no encoder library)
- Button debounce: Bounce2 library
- No FreeRTOS — cooperative scheduling with `millis()` timers

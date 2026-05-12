# Testing Guide

## Arduino IDE setup (one time)

1. Install **Arduino IDE 2.x** from https://www.arduino.cc/en/software
2. Open **File → Preferences**, paste into "Additional boards manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif**
4. Open **Tools → Manage Libraries**, install:
   - `U8g2` by olikraus
   - `DFRobotDFPlayerMini` by DFRobot
   - `Bounce2` by Thomas O Fredericks
5. Plug in the ESP32 via Micro-USB
6. **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
7. **Tools → Port** → select the COM/tty port that appeared when you plugged in

---

## Sketch

There is one sketch: `turbo/turbo.ino`.

Open it in Arduino IDE and upload to the ESP32.

---

## SD card preparation

1. Format the microSD card as **FAT32**
2. Create a folder called `mp3` on the card
3. Put your Turbo sound file in `/mp3/0001.mp3`
   (Download a free sample — search "turbo blow off valve sound effect mp3")
4. Insert card into DFPlayer Mini

---

## Wiring

### OLED (SSD1306)

| OLED pin | ESP32 pin |
| -------- | --------- |
| SDA      | GPIO 21   |
| SCL      | GPIO 22   |
| VCC      | 3.3V      |
| GND      | GND       |

### KY-040 rotary encoder

| KY-040 pin | ESP32 pin |
| ---------- | --------- |
| CLK        | GPIO 25   |
| DT         | GPIO 26   |
| SW         | GPIO 27   |
| +          | 3.3V      |
| GND        | GND       |

### DFPlayer Mini

| DFPlayer pin | ESP32 pin                |
| ------------ | ------------------------ |
| TX           | GPIO 16 (ESP32 RX2)      |
| RX           | GPIO 17 via 1kΩ resistor |
| VCC          | 5V (VIN on ESP32 board)  |
| GND          | GND                      |
| SPK1 / SPK2  | Speaker (4–8 Ω)          |

> The 1kΩ resistor on DFPlayer RX protects it from the ESP32's 3.3V logic.
> DFPlayer needs 5V — connect VCC to VIN, not 3.3V.

---

## First run — without the car

Turn the ignition to ON (dashboard lights up, don't start the engine).

Expected on OLED:
- Boot screen "Turbo Emulator" → "Scanning BT..."
- Connects to ELM327 within ~15 seconds
- Live throttle/speed/RPM gauges

Encoder:
- **Rotate** → cycle through 4 views: Throttle / Speed / All metrics / Dual bars
- **Click** → disconnect Bluetooth and return to scan screen

---

## Turbo trigger test — in the car

1. Start engine, drive in 1st gear above ~1500 RPM with >40% throttle
2. Lift off sharply (as if pressing the clutch to change gear)
3. Speaker should play the Turbo sound

Serial monitor (115200 baud) prints `[Turbo] #N gear=G TPS X→Y RPM Z` every time it fires.

**Tuning** — edit constants at the top of `turbo.ino`:

```cpp
#define TURBO_THROTTLE_HIGH  40.0f   // lower = triggers more easily
#define TURBO_THROTTLE_LOW   10.0f   // higher = triggers more easily
#define TURBO_RPM_MIN        1500.0f // lower = triggers at lower revs
#define TURBO_MAX_GEAR       2       // raise to allow higher gears
```

---

## OBD2 connection troubleshooting

- ELM327 LED should be lit/blinking — if not, it's not getting power from the OBD port
- Serial monitor lists every Bluetooth device found during the scan — check the name
  matches what `doScanning()` looks for ("ELM", "OBD", "LINK")
- If PIN fails, add your dongle's PIN to the `pins[]` array in `doConnecting()`
- If you see `STOPPED` or `?` responses, change `ATSP0` to `ATSP6` (ISO 15765-4 CAN)

---

## Wokwi simulation

See the main [README.md](../README.md#wokwi-simulation) for simulation setup.

```bash
make wokwi-setup   # one time
make wokwi-build   # after any sketch change
```

Then open `Emulators/Wokwi/diagram.json` in VS Code and press
**F1 → Wokwi: Start Simulator**.

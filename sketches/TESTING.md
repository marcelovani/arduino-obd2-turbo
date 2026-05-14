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

Breadboard columns run **J (left) → A (right)**; left bank = cols J–F, right bank = cols E–A.
The ESP32 spans **col I (left pins) to col A (right pins), rows 5–19**, USB end up.

**Power rails**

| Wire | From               | To             | Note  |
| ---- | ------------------ | -------------- | ----- |
| 3V3  | J5 (=I5 ESP32 3V3) | Left rail (+)  | Short |
| GND  | J6 (=I6 ESP32 GND) | Left rail (−)  | Short |
| 5V   | A5 (ESP32 VIN)     | Right rail (+) | Short |
| GND  | A6 (ESP32 GND)     | Right rail (−) | Short |

### OLED (SSD1306 SPI)

OLED 7-pin header plugs into **col F, rows 42–48** (one pin per row).

| OLED pin  | ESP32 pin | From | To            | ~Wire |
| --------- | --------- | ---- | ------------- | ----- |
| GND       | GND       | F42  | Left rail (−) | 2 cm  |
| VCC       | 3.3V      | F43  | Left rail (+) | 2 cm  |
| D0 (SCK)  | GPIO 18   | F44  | J13           | 11 cm |
| D1 (MOSI) | GPIO 23   | F45  | J19           | 10 cm |
| RES       | GPIO 15   | F46  | J7            | 13 cm |
| DC        | GPIO 32   | F47  | A14           | 15 cm |
| CS        | GPIO 5    | F48  | J12           | 13 cm |

> SCK/MOSI/RES/CS all route up the left bank (cols F→J).
> DC crosses the full board (left bank F → right bank A) — route under the board.

### KY-040 rotary encoder

Encoder plugs into **col E, rows 30–34** (one pin per row).

| KY-040 pin | ESP32 pin | From | To                   | ~Wire |
| ---------- | --------- | ---- | -------------------- | ----- |
| GND        | GND       | E30  | Right rail (−)       | 2 cm  |
| +          | 3.3V      | E31  | Left rail (+) row 31 | 5 cm  |
| SW         | GPIO 27   | E32  | A10                  | 8 cm  |
| DT         | GPIO 26   | E33  | A11                  | 8 cm  |
| CLK        | GPIO 25   | E34  | A12                  | 8 cm  |

> Encoder `+` crosses row 31 from right bank (E) to left 3.3V rail.
> SW/DT/CLK run up the right bank (cols E→A).

### DFPlayer Mini

DFPlayer straddles the full board width — **left pins col I, right pins col A, rows 52–59**.
Only left-side pins are wired for audio/data.

| DFPlayer pin | ESP32 pin                | From      | To                   | ~Wire |
| ------------ | ------------------------ | --------- | -------------------- | ----- |
| VCC          | 3.3V                     | I52       | Left rail (+)        | 2 cm  |
| RX           | GPIO 17 via 1kΩ resistor | I53       | H36 (resistor leg 2) | 6 cm  |
| TX           | GPIO 16 (RX2)            | I54       | J10                  | 14 cm |
| GND          | GND                      | I58       | Left rail (−)        | 2 cm  |
| SPK1 / SPK2  | Mini-amp                 | I57 / I59 | Amp input            | —     |
| GND (right)  | GND                      | A58       | Right rail (−)       | 2 cm  |

**1 kΩ resistor — DFPlayer RX line**

Place resistor vertically at **col H, rows 35–36** (free area between encoder and OLED):

| Leg | Hole | Wire                                           |
| --- | ---- | ---------------------------------------------- |
| 1   | H35  | J11 → H35 (~8 cm) — J11 = I11 = ESP32 IO17/TX2 |
| 2   | H36  | H36 → I53 (~6 cm) — I53 = DFPlayer RX          |

> DFPlayer VCC spec is 3.2–5V — powered at 3.3V from the left rail.
> TX wire (I54 → J10, 14 cm) runs up the left bank col I→J.

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

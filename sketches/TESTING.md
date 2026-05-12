# Testing Guide — Phase by Phase

## Before anything: Arduino IDE setup

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
   - `Adafruit MPU6050` by Adafruit
   - `Adafruit Unified Sensor` by Adafruit
5. Plug in the ESP32 via Micro-USB
6. **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
7. **Tools → Port** → select the COM/tty port that appeared when you plugged in

---

## Phase 1a — OLED display (`phase1_oled/`)

**Wiring**

| OLED pin | ESP32 pin |
| -------- | --------- |
| SDA      | GPIO 21   |
| SCL      | GPIO 22   |
| VCC      | 3.3V      |
| GND      | GND       |

**Deploy**
Open `phase1_oled/phase1_oled.ino` → Upload (→ button).

**Expected result**
- Screen shows "OBD2 Turbo", "Display OK!", and a counter incrementing every 500ms.
- Serial monitor (115200 baud) prints `Frame N`.

**If nothing shows / garbage**
- Swap the constructor to `U8G2_SH1106_128X64_NONAME_F_HW_I2C` and re-upload.
- Check SDA/SCL aren't swapped.
- Check VCC is 3.3V not 5V.

---

## Phase 1b — IMU (`phase1_imu/`)

**Wiring** (add MPU6050 to the same I2C bus as OLED)

| MPU6050 pin | ESP32 pin |
| ----------- | --------- |
| SDA         | GPIO 21   |
| SCL         | GPIO 22   |
| VCC         | 3.3V      |
| GND         | GND       |

**Deploy**
Open `phase1_imu/phase1_imu.ino` → Upload.

**Expected result**
- OLED shows X/Y/Z in G-force, updating 10× per second.
- Board flat on desk: X ≈ 0, Y ≈ 0, Z ≈ 0.
- Tilt the board: values change.
- Serial monitor shows the same values.

**If "MPU6050 not found"**
- Check SDA/SCL wiring.
- Some MPU6050 modules have AD0 pulled high (address 0x69). If so, add `mpu.begin(0x69)`.

---

## Phase 1c — Rotary encoder (`phase1_encoder/`)

**Wiring**

| KY-040 pin | ESP32 pin |
| ---------- | --------- |
| CLK        | GPIO 25   |
| DT         | GPIO 26   |
| SW         | GPIO 27   |
| +          | 3.3V      |
| GND        | GND       |

**Deploy**
Open `phase1_encoder/phase1_encoder.ino` → Upload.

**Expected result**
- OLED shows current position (starts at 0).
- Rotate CW → position increments, shows "CW".
- Rotate CCW → position decrements, shows "CCW".
- Click → position resets to 0, shows "CLICK — reset".
- Serial monitor mirrors all events.

**If rotation goes wrong direction**
- Swap CLK and DT pins (or flip the `1` and `-1` in the sketch).

---

## Phase 1d — DFPlayer Mini (`phase1_dfplayer/`)

**SD card preparation (do this before wiring)**
1. Format the microSD card as **FAT32**.
2. Create a folder called `mp3` on the card.
3. Put your Turbo sound file in `/mp3/0001.mp3`.
   (Download a free Turbo sample — search "turbo blow off valve sound effect mp3".)
4. Insert card into DFPlayer Mini.

**Wiring**

| DFPlayer pin | ESP32 pin                  |
| ------------ | -------------------------- |
| TX           | GPIO 16 (ESP32 RX2)        |
| RX           | GPIO 17 via 1kΩ resistor   |
| VCC          | 5V (VIN on ESP32 board)    |
| GND          | GND                        |
| SPK1 / SPK2  | Speaker (4–8 Ω)            |

> The 1kΩ resistor on DFPlayer RX protects it from the ESP32's 3.3V logic.

**Deploy**
Open `phase1_dfplayer/phase1_dfplayer.ino` → Upload.

**Expected result**
- OLED shows "DFPlayer ready!" and "Click to play".
- Press the encoder button → speaker plays the Turbo sound.
- Serial monitor confirms "Playing track 1".

**If "DFPlayer FAILED"**
- SD card not inserted, or not FAT32, or `/mp3/0001.mp3` doesn't exist.
- TX/RX wired correctly (DFPlayer TX → ESP32 RX, not the other way).
- DFPlayer needs 5V — make sure VCC goes to VIN (5V), not 3.3V.

---

## Phase 2 — OBD2 Bluetooth connection (`phase2_obd2/`)

**Hardware needed:** all of Phase 1 wired up + ELM327 dongle in car OBD2 port.

**Deploy**
Open `phase2_obd2/phase2_obd2.ino` → Upload.

**Bluetooth PIN**
The sketch automatically tries PIN `1234` then `0000` — both common ELM327 defaults.
OLED shows which PIN is being tried. No manual pairing needed.

**Connection feedback on OLED**
- `Scanning BT…` — scanning for devices
- `Found! <name>` — dongle detected
- `Connecting… PIN: 1234` — attempting connection
- `ELM327 ready! <name>` — fully connected and initialised
- Small **filled dot** in the top-right corner of every screen = BT connected
- Small **hollow circle** = not connected

**Testing without starting the engine**
Turn the ignition to ON (dashboard lights up) but don't start the engine.
The following parameters work immediately:

| Parameter        | What you see                       | Notes                     |
| ---------------- | ---------------------------------- | ------------------------- |
| Battery voltage  | `BATT: 12.4V`                      | ~12V off, ~14V running    |
| Fuel level       | `FUEL: 75%`                        | Approximate               |
| Coolant temp     | `COOL: 85C`                        | Current engine temp       |
| Throttle (TPS)   | `TPS: 0%` → press pedal to verify  | Works with ignition on    |
| Speed / RPM      | Shows 0 until engine is running    | Need engine running       |

**Procedure**
1. Plug ELM327 into OBD2 port (under dashboard, driver's side).
2. Turn ignition ON (engine off is fine for first test).
3. Power ESP32 via USB.
4. OLED auto-scans, connects, and shows all values within ~15 seconds.
5. Serial monitor (115200 baud) shows full detail including AT responses.

**If it doesn't connect**
- ELM327 LED should be lit/blinking — if not, it's not getting power from OBD port.
- Check serial monitor — it prints every device found during the scan with its name.
  If your dongle has an unusual name, add it to the `upper.indexOf(...)` check in the sketch.
- If PIN fails, check your dongle's manual — some use `6789` or `8888`.
  Add the PIN to the `pins[]` array in `connectWithPin()`.
- If you see `STOPPED` or `?` responses from ELM327, the OBD protocol wasn't auto-detected.
  Change `ATSP0` to `ATSP6` (ISO 15765-4 CAN) which most modern cars use.

---

## Phase 3 — Turbo trigger (`phase3_turbo/`)

**Hardware needed:** everything from Phase 2, plus DFPlayer Mini + speaker.

**Deploy**
Open `phase3_turbo/phase3_turbo.ino` → Upload.

**Procedure**
1. Full setup in the car with engine running.
2. Accelerate in 1st gear past ~1500 RPM with >40% throttle.
3. Lift off sharply (as if pressing clutch to change gear).
4. Speaker should play the Turbo sound.

**Tuning**
If the Turbo triggers too often or not enough, edit the constants at the top of the sketch:
```cpp
#define TURBO_THROTTLE_HIGH  40.0f   // lower = triggers more easily
#define TURBO_THROTTLE_LOW   10.0f   // higher = triggers more easily
#define TURBO_RPM_MIN        1500.0f // lower = triggers at lower revs
```
Re-upload after each change. Serial monitor shows `Turbo! TPS X→Y% RPM Z Gear N` every time it fires.

**Gear thresholds**
The `estimateGear()` function uses rough RPM/speed ratios. After you've driven in each gear, note the RPM and speed values from the serial monitor and adjust the thresholds in the function to match your CLA180.

---

## Phase 4 — Full dashboard (`phase4_dashboard/`)

**Hardware needed:** everything — ESP32, OLED, MPU6050, KY-040, DFPlayer Mini, speaker, ELM327.

**Deploy**
Open `phase4_dashboard/phase4_dashboard.ino` → Upload.

**Controls**
- **Rotate encoder** → cycle through 4 views: Throttle / Speed / All metrics / G-force
- **Click encoder** → disconnect Bluetooth and return to scan screen

**Views**
1. Large throttle % + current gear + speed
2. Large speed + gear + RPM
3. All metrics as text + Turbo count
4. G-force reading + speed + RPM

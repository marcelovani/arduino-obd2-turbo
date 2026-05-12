# Arduino OBD2 Turbo Blow-Off Valve Emulator — Project Plan

A device that reads live OBD2 data from the car and plays a turbo
blow-off valve (Turbo) "pssssh" sound through a speaker whenever the driver
lifts off the throttle at speed during a gear change — emulating the iconic
Fast & Furious turbo sound.

The same hardware also displays live gauges (throttle, RPM, speed, G-force)
on a small OLED screen so the driver can see what's happening in real time.

---

## 1. What we're building

A portable device that:

1. Connects via Bluetooth Classic to a generic ELM327 OBD2 dongle
2. Reads live signals from the car:
   - **Throttle position** (% pedal pressed)
   - **Speed** (km/h)
   - **RPM** (used for gear calculation and Turbo trigger)
   - **Gear** (calculated from RPM ÷ speed ratio)
3. Reads one signal from the ESP32 itself:
   - **Acceleration** (G-force relative to 1g, from MPU6050 IMU)
4. **Plays a Turbo sound** via DFPlayer Mini + speaker when:
   - Throttle drops rapidly from high to low (driver lifted off / gear change)
   - RPM is aturboe the boost threshold (~1500 RPM)
   - Current gear is 1st or 2nd (configurable)
5. Displays live gauges on a 0.96" OLED screen
6. KY-040 rotary encoder to cycle between display views

**Target car:** Mercedes CLA180, 2011, gasoline, **manual gearbox**

---

## 2. How the Turbo sound trigger works

A turbo blow-off valve vents pressurised intake air when the driver lifts
off the throttle (closing the throttle plate while the turbo is still
spinning). The escaping air makes the characteristic "pssssh" / "ksss"
sound. This device detects that moment from OBD2 data and plays a
pre-recorded Turbo sample through the speaker.

### Trigger algorithm

```
every 100 ms (OBD poll cycle):

  if (throttle_prev  > TURBO_THROTTLE_HIGH   // was accelerating (e.g. >40%)
  AND throttle_now   < TURBO_THROTTLE_LOW    // now lifted off    (e.g. <10%)
  AND rpm_now        > TURBO_RPM_MIN         // in boost range    (e.g. >1500)
  AND current_gear  <= TURBO_MAX_GEAR        // 1st or 2nd gear
  AND time_since_last_turbo > TURBO_COOLDOWN): // not already playing
      play Turbo sound
      record timestamp
```

### Tunable thresholds (constants in code)

| Constant            | Default | Meaning                                    |
| ------------------- | ------- | ------------------------------------------ |
| `TURBO_THROTTLE_HIGH` | 40 %    | Throttle must have been aturboe this         |
| `TURBO_THROTTLE_LOW`  | 10 %    | Throttle must now be below this            |
| `TURBO_RPM_MIN`       | 1500    | Minimum RPM to trigger (in boost)          |
| `TURBO_MAX_GEAR`      | 2       | Only trigger in gears ≤ this               |
| `TURBO_COOLDOWN_MS`   | 2000 ms | Minimum gap between consecutive Turbo sounds |

These can be adjusted to taste once tested in the car.

---

## 3. Bill of materials

| Item                             | Approx price (UK) | Status   | Notes                                                       |
| -------------------------------- | ----------------- | -------- | ----------------------------------------------------------- |
| ELEGOO ESP-WROOM-32 DevKit       | £6–8              | To order | Bluetooth Classic + BLE + WiFi. [Amazon link][amz-esp32]    |
| 0.96" SSD1306 OLED (128x64, I2C) | —                 | Have it  | Model ep0096dtan001a. I2C address 0x3C                      |
| MPU6050 IMU module (I2C)         | —                 | Have it  | 3-axis accelerometer + gyro, address 0x68                   |
| KY-040 rotary encoder module     | —                 | Have it  | 5-pin: CLK / DT / SW / + / GND                              |
| DFPlayer Mini MP3 module         | —                 | Have it  | Same module used in [arduino-laser-target][laser-target]    |
| microSD card (≤32 GB, FAT32)     | —                 | Check    | Must be formatted FAT32; Turbo .mp3 file goes in /mp3/ folder |
| Small speaker (4–8 Ω, 0.5–3 W)   | £2–3              | Check    | Connects to DFPlayer Mini's built-in 3W amp (SPK1/SPK2)     |
| Breadboard + jumper wires        | —                 | Have it  |                                                             |
| Micro-USB cable                  | —                 | Have it  | Power + flashing                                            |
| ELM327 Bluetooth OBD2 dongle     | —                 | Have it  | Bluetooth Classic. [Manufacturer site][elm-home]            |
| **Total new spend**              | **~£10**          |          | ESP32 + speaker (if needed)                                 |

### Hardware decisions (from planning session)

- **ESP8266 rejected** — no Bluetooth at all.
- **ESP32-C3 SuperMini rejected** — BLE only, no Bluetooth Classic.
- **ELEGOO ESP-WROOM-32 chosen** — Bluetooth 4.2 Classic + BLE.
- **KY-040 rotary encoder** replaces original 3-button design.
- **0.96" SSD1306 OLED** confirmed (model ep0096dtan001a).
- **ELM327 confirmed Bluetooth Classic only**.
- **DFPlayer Mini** reused from [arduino-laser-target][laser-target] project.

### Optional upgrades

- Replace OLED with a **1.8" TFT (ST7735, SPI)** for colour charts
- Add a **12V→5V car USB adapter** for permanent install (~£3)
- 3D-printed enclosure for dash mounting

---

## 4. Architecture

```
┌─────────────────────────────────────────────────────┐
│                      ESP32                          │
│                                                     │
│  ┌────────────┐         ┌────────────────┐          │
│  │  Bluetooth │ ◄──BT── │  ELM327 dongle │ ── OBD2 ── Car ECU
│  │  Classic   │         └────────────────┘          │
│  └─────┬──────┘                                     │
│        │                                            │
│        ▼                                            │
│  ┌─────────────┐    ┌─────────────────┐             │
│  │ OBD Service │    │  IMU (MPU6050)  │             │
│  │ (PID poller)│    │  via I2C        │             │
│  └─────┬───────┘    └────────┬────────┘             │
│        │                     │                      │
│        └──────────┬──────────┘                      │
│                   ▼                                 │
│           ┌───────────────┐                         │
│           │   App state   │                         │
│           │  (ring buffer)│                         │
│           └───────┬───────┘                         │
│                   │                                 │
│      ┌────────────┼─────────────┐                   │
│      ▼            ▼             ▼                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐     │
│  │  OLED    │ │  KY-040  │ │  Turbo Trigger     │     │
│  │ (gauges) │ │ encoder  │ │  → DFPlayer Mini │     │
│  └──────────┘ └──────────┘ └────────┬─────────┘     │
│                                     │               │
└─────────────────────────────────────┼───────────────┘
                                      ▼
                               ┌─────────────┐
                               │  Speaker    │
                               │  (Turbo sound)│
                               └─────────────┘
```

### Key design choices

- **Bluetooth Classic** — works with the ELM327 dongle via `BluetoothSerial`.
- **Auto-connect** — scans for any BT device containing "ELM", "OBD", or
  "LINK" in the name; connects without user input.
- **Turbo trigger is event-driven** — checked every OBD poll cycle (100 ms);
  compares current vs previous throttle reading.
- **DFPlayer Mini** — standalone MP3 module with its own SD card; the ESP32
  sends a single UART command to trigger playback. No audio processing on
  the ESP32 itself.
- **KY-040 encoder** — CLK/DT read manually; SW debounced with Bounce2 (same
  pattern as [arduino-laser-target][laser-target]).
- **G-force relative to 1g** — IMU Z-axis has gravity subtracted so a
  stationary reading is ~0 G.

---

## 5. Wiring

| Component       | ESP32 pin | Notes                                      |
| --------------- | --------- | ------------------------------------------ |
| OLED SDA        | GPIO 21   | I2C SDA (shared bus)                       |
| OLED SCL        | GPIO 22   | I2C SCL (shared bus)                       |
| OLED VCC        | 3.3V      |                                            |
| OLED GND        | GND       |                                            |
| MPU6050 SDA     | GPIO 21   | Same I2C bus, address 0x68                 |
| MPU6050 SCL     | GPIO 22   |                                            |
| MPU6050 VCC     | 3.3V      |                                            |
| MPU6050 GND     | GND       |                                            |
| KY-040 CLK      | GPIO 25   | Rotary pulse A                             |
| KY-040 DT       | GPIO 26   | Rotary pulse B                             |
| KY-040 SW       | GPIO 27   | Push-button, INPUT_PULLUP                  |
| KY-040 +        | 3.3V      |                                            |
| KY-040 GND      | GND       |                                            |
| DFPlayer TX     | GPIO 16   | ESP32 RX2 (receives from DFPlayer)         |
| DFPlayer RX     | GPIO 17   | ESP32 TX2 (sends commands to DFPlayer)     |
| DFPlayer VCC    | 5V        | DFPlayer needs 5V (use VIN on ESP32 board) |
| DFPlayer GND    | GND       |                                            |
| DFPlayer SPK1/2 | Speaker   | Direct speaker connection (4–8 Ω)          |

> **Note:** DFPlayer RX has a 1kΩ resistor in series to protect it from ESP32
> 3.3V logic — the DFPlayer is a 5V device. Many modules tolerate 3.3V TX
> directly, but the resistor is safer.

---

## 6. Software stack

### Toolchain

- **Arduino IDE 2.x**
- ESP32 board support:
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Board: **ESP32 Dev Module**

### Libraries (Library Manager)

| Library                   | Purpose                                          |
| ------------------------- | ------------------------------------------------ |
| `BluetoothSerial`         | Built into ESP32 core                            |
| `U8g2`                    | OLED — use `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` |
| `DFRobotDFPlayerMini`     | DFPlayer Mini MP3 module driver                  |
| `Bounce2`                 | KY-040 button debounce                           |
| `Adafruit MPU6050`        | IMU driver                                       |
| `Adafruit Unified Sensor` | Dependency of MPU6050 lib                        |

> **Note:** The [arduino-laser-target][laser-target] reference project uses
> `U8glib` (older) and `DFRobotDFPlayerMini`. We use **U8g2** (same author,
> newer, better ESP32 support) and the same DFPlayer library.

### SD card setup for DFPlayer

1. Format microSD card as **FAT32**
2. Create folder `/mp3/`
3. Name the Turbo sound file `0001.mp3` (DFPlayer uses numeric filenames)
4. Optionally add more sounds: `0002.mp3` = softer Turbo, `0003.mp3` = longer, etc.

---

## 7. The OBD2 protocol

Once connected over Bluetooth Serial, the ELM327 behaves like a text
terminal. Each command ends with `\r`. Each response ends with `>`.

### Initialisation (one-time, on connect)

| Command   | Effect                              |
| --------- | ----------------------------------- |
| `ATZ\r`   | Reset (allow 3 s — resets firmware) |
| `ATE0\r`  | Echo off                            |
| `ATL0\r`  | Linefeeds off                       |
| `ATS0\r`  | Spaces off (compact hex responses)  |
| `ATH0\r`  | Hide headers                        |
| `ATSP0\r` | Auto-detect car protocol            |

### Live queries (polling loop)

| PID      | Command  | Response   | Formula          | Range      |
| -------- | -------- | ---------- | ---------------- | ---------- |
| Throttle | `0111\r` | `4111XX`   | `XX × 100 / 255` | 0–100 %    |
| Speed    | `010D\r` | `410DXX`   | `XX`             | 0–255 km/h |
| RPM      | `010C\r` | `410CXXYY` | `(XX×256+YY)/4`  | 0–16383    |

### Gear calculation

OBD2 does not expose gear directly. Calculated from RPM ÷ speed ratio.
For the **Mercedes CLA180 (2011, gasoline)** gear ratios will be calibrated
empirically — drive steadily in each gear and log the `RPM/speed` plateau.
v0 shows the raw ratio; v1 maps it to a gear number.

---

## 8. State machine (UI flow)

```
[BOOT]
  │
  ▼
[SCANNING]  ──── auto-scans BT, shows "Scanning…" on OLED
  │                 connects automatically on ELM/OBD/LINK name match
  ▼
[CONNECTING] ───── "Connecting to <name>…"
  │                  failure → SCANNING
  ▼
[INIT_ELM]  ────── AT command sequence, "Initialising…"
  │                  failure → SCANNING
  ▼
[RUNNING]   ────── live gauges + Turbo trigger active
               rotate encoder → cycle views:
                 1. Throttle (large) + Speed
                 2. Speed (large) + RPM
                 3. All metrics (text)
                 4. G-force
               click encoder → disconnect, back to SCANNING
```

---

## 9. Polling loop timing

```
every 100 ms:
    poll throttle, speed, RPM  →  ring buffers
    check Turbo trigger

every 20 ms:
    sample IMU  →  ring buffer

every 50 ms:
    redraw OLED
```

---

## 10. Build plan — phased

### Phase 1: Hardware verification (day 1)

- [ ] Flash "Blink" — confirm ESP32 toolchain
- [ ] Wire OLED, run `oled_test.ino` — confirm SSD1306 displays counter
- [ ] Wire MPU6050, confirm acceleration values in serial monitor
- [ ] Wire KY-040, confirm rotation direction + click in serial monitor
- [ ] Wire DFPlayer Mini, play `0001.mp3` from SD card

**Done when:** Turning the encoder prints direction, OLED shows a counter,
and the speaker plays the Turbo sound on demand.

### Phase 2: OBD2 connection (day 2)

- [ ] Bluetooth pair with ELM327
- [ ] AT init sequence, confirm responses in serial monitor
- [ ] Poll throttle, speed, RPM — confirm live values with engine running

**Done when:** Serial monitor shows live throttle/speed/RPM in the car.

### Phase 3: Turbo trigger (day 3) — core feature

- [ ] Implement throttle-drop detection algorithm
- [ ] Trigger DFPlayer on Turbo event
- [ ] Test in car: accelerate in 1st, lift off — Turbo sound plays
- [ ] Tune `TURBO_THROTTLE_HIGH`, `TURBO_THROTTLE_LOW`, `TURBO_RPM_MIN`

**Done when:** The Turbo sound plays naturally on every gear change.

### Phase 4: Display + full UI (day 4)

- [ ] State machine: SCANNING → CONNECTING → INIT_ELM → RUNNING
- [ ] Auto-connect logic
- [ ] Live gauges on OLED (throttle, speed, RPM, gear)
- [ ] Encoder navigation between views

### Phase 5: Gear calibration + refinement

- [ ] Log RPM/speed ratios per gear for the CLA180
- [ ] Map ratios to gear numbers
- [ ] Restrict Turbo to gear 1 and 2 only
- [ ] Add cooldown to prevent double-trigger

### Phase 6: Polish (optional)

- [ ] Persist ELM327 address in NVS (auto-reconnect on boot)
- [ ] Multiple Turbo sounds, play randomly for variety
- [ ] Adjustable volume via encoder long-press menu
- [ ] 3D-printed enclosure
- [ ] Bluetooth audio — stream Turbo sound to car radio via a BT A2DP
      transmitter module on DFPlayer DAC pins (ESP32's BT is occupied by OBD2,
      so a separate BT audio module is needed for this)

---

## 11. Risks and gotchas

| Risk                                    | Mitigation                                                                                                         |
| --------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Cheap ELM327 clones have buggy firmware | Test `ATZ` early — should return `ELM327 v1.5`. If garbage, swap the dongle.                                       |
| Bluetooth pairing fiddly first time     | Use `discover()` API with 8 s scan. All retry logic is automatic.                                                  |
| Some cars don't support some PIDs       | Send `0100` first — bitmap of supported PIDs. Only poll supported ones.                                            |
| Polling too fast → `NO DATA` responses  | Cap at 10 Hz total across 3 PIDs. Use `ATAT1` for adaptive timing.                                                 |
| `\r` vs `\r\n` mismatch with ELM327     | Always send `\r` only; wait for `>` before sending next command.                                                   |
| OLED flicker                            | Use U8g2 full-buffer mode (`_F_`).                                                                                 |
| ESP32 brown-out during engine crank     | Power from a buffered USB socket; add capacitor on 5V rail.                                                        |
| ATZ takes up to 3 s                     | Use 3000 ms timeout for ATZ; 1000 ms for all other AT commands.                                                    |
| DFPlayer busy / sound cut short         | Check BUSY pin (DFPlayer GPIO 16) before triggering next sound; respect cooldown timer.                            |
| Turbo triggers too eagerly or not enough  | Tune the four `TURBO_*` threshold constants after first real-car test.                                               |
| G-force shows ~1 g at rest              | Subtract gravity (≈9.81 m/s²) from Z-axis before display.                                                          |
| Manual gearbox — no clutch PID on OBD2  | Throttle-drop detection is the trigger (driver lifts off when pressing clutch). No need to detect clutch directly. |

---

## 12. References

### Turbo / turbo sounds

- **Reddit — what causes the "ksss" sound in Fast & Furious:**
  https://www.reddit.com/r/cars/comments/rl8a5e/what_causes_the_ksss_sound_in_the_fast_furious_1/
- **YouTube — how turbo noises work:**
  https://www.youtube.com/watch?v=S3QYMqCVsdg

### ELM327 / OBD2

- **ELM327 manufacturer home:** https://www.elm327.com/index.php
- **ELM327 OBD software development guide:** https://www.elm327.com/Support/2.html
- **ELM327 AT command datasheet:** https://www.elmelectronics.com/wp-content/uploads/2016/07/ELM327DS.pdf
- **OBD-II PID list with formulas:** https://en.wikipedia.org/wiki/OBD-II_PIDs
- **Complete PID reference:** https://github.com/evrenonur/obd2-elm327-pid-reference

### ESP32 / Arduino

- **ESP32 BluetoothSerial docs:** https://github.com/espressif/arduino-esp32/tree/master/libraries/BluetoothSerial
- **ELEGOO ESP-WROOM-32 (chosen board):** https://www.amazon.co.uk/ELEGOO-ESP-WROOM-32-Development-Micro-USB-Microcontroller/dp/B0D8T5P8JM
- **ESP32-C3 SuperMini (rejected — BLE only):** https://www.amazon.co.uk/Youmile-Development-SuperMini-Bluetooth-Frequency/dp/B0D2WSYRZM

### Display, audio, encoder

- **U8g2 graphics library:** https://github.com/olikraus/u8g2/wiki
- **DFPlayer Mini wiki:** https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299

### Reference projects

- **arduino-laser-target** (same SSD1306 + KY-040 + DFPlayer Mini pattern):
  https://github.com/marcelovani/arduino-laser-target

### Testing tools

- **ELM327 emulator (test without a car):** https://github.com/Ircama/ELM327-emulator

---

## 13. Code skeleton

```cpp
// arduino-obd2-turbo — main sketch
// Libraries: BluetoothSerial (built-in), U8g2, DFRobotDFPlayerMini, Bounce2,
//            Adafruit MPU6050, Adafruit Unified Sensor

#include <BluetoothSerial.h>
#include <U8g2lib.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_MPU6050.h>
#include <Bounce2.h>

// ── Pins ─────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27
#define PIN_DFP_RX   16   // ESP32 RX2 ← DFPlayer TX
#define PIN_DFP_TX   17   // ESP32 TX2 → DFPlayer RX

// ── Turbo thresholds (tune after first car test) ────────────────────────────
#define TURBO_THROTTLE_HIGH  40.0f   // % — was accelerating
#define TURBO_THROTTLE_LOW   10.0f   // % — now lifted off
#define TURBO_RPM_MIN        1500.0f // RPM — in boost range
#define TURBO_MAX_GEAR       2       // only gears 1 and 2
#define TURBO_COOLDOWN_MS    2000    // ms between sounds

// ── Objects ───────────────────────────────────────────────────────────────
BluetoothSerial BT;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
DFRobotDFPlayerMini dfplayer;
Adafruit_MPU6050 mpu;
Bounce encBtn;

// ── State ─────────────────────────────────────────────────────────────────
enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING };
AppState appState = SCANNING;

// ── Metrics ───────────────────────────────────────────────────────────────
float metricTPS   = 0;
float metricSpeed = 0;
float metricRPM   = 0;
float metricGear  = 0;
float metricGforce= 0;
float prevTPS     = 0;

// ── Turbo ───────────────────────────────────────────────────────────────────
uint32_t lastTurboMs = 0;

void checkTurboTrigger() {
    uint32_t now = millis();
    if (now - lastTurboMs < TURBO_COOLDOWN_MS) return;

    if (prevTPS      > TURBO_THROTTLE_HIGH &&
        metricTPS    < TURBO_THROTTLE_LOW  &&
        metricRPM    > TURBO_RPM_MIN       &&
        metricGear   > 0                 &&
        metricGear  <= TURBO_MAX_GEAR) {
        dfplayer.play(1);   // play /mp3/0001.mp3
        lastTurboMs = now;
        Serial.println("Turbo triggered!");
    }
    prevTPS = metricTPS;
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);

    Wire.begin();
    oled.begin();
    mpu.begin();

    dfplayer.begin(Serial2);
    dfplayer.volume(25);   // 0–30

    pinMode(PIN_ENC_CLK, INPUT);
    pinMode(PIN_ENC_DT,  INPUT);
    encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
    encBtn.interval(25);

    BT.begin("ESP32-OBD", true);  // Bluetooth Classic master
}

void loop() {
    switch (appState) {
        case SCANNING:   handleScanning();   break;
        case CONNECTING: handleConnecting(); break;
        case INIT_ELM:   handleInitElm();    break;
        case RUNNING:    handleRunning();    break;
    }
}
// (full state handlers written in Phase 3–4)
```

---

## 14. Next steps

When the ELEGOO ESP32 arrives:

1. Flash "Blink" — confirm toolchain
2. Run `oled_test.ino` — confirm display wiring
3. Wire DFPlayer Mini, load `0001.mp3` onto SD card, confirm sound plays
4. **Only then** tackle Bluetooth + OBD2

[amz-esp32]: https://www.amazon.co.uk/ELEGOO-ESP-WROOM-32-Development-Micro-USB-Microcontroller/dp/B0D8T5P8JM
[elm-home]: https://www.elm327.com/index.php
[laser-target]: https://github.com/marcelovani/arduino-laser-target

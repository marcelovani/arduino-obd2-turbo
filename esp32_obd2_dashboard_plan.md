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

1. Connects via BLE to an "OBD BLE" dongle (service FFF0, notify FFF1, write FFF2)
2. Reads live signals from the car via OBD2 (see three-tier polling in §7–9):
   - **RPM** — engine state detection and gear calculation
   - **Throttle position** — % pedal pressed (driving mode only)
   - **Speed** — km/h, used for gear estimation (driving mode only)
   - **Gear** — calculated from RPM ÷ speed ratio
   - **Battery voltage** and **coolant temperature** — shown on parked screen
3. **Plays a Turbo sound** via DFPlayer Mini + speaker when:
   - Throttle drops rapidly from high to low (driver lifted off / gear change)
   - RPM is aturboe the boost threshold (~1500 RPM)
   - Current gear is 1st or 2nd (configurable)
4. Displays live gauges on a 0.96" OLED screen
5. KY-040 rotary encoder to cycle between display views

**Target car:** Mercedes CLA180, 2011, gasoline, **manual gearbox**

---

## 2. How the Turbo sound trigger works

A turbo blow-off valve vents pressurised intake air when the driver lifts
off the throttle (closing the throttle plate while the turbo is still
spinning). The escaping air makes the characteristic "pssssh" / "ksss"
sound. This device detects that moment from OBD2 data and plays a
pre-recorded Turbo sample through the speaker.

The trigger is only active in **driving mode** (RPM ≥ 1000). Below that
threshold, throttle and speed are not polled at all — there is no point
checking for a gear-change lift-off at idle or with the engine off.

### Trigger algorithm

```
every 100 ms (driving mode only — RPM ≥ 1000):

  if (throttle_prev  > TURBO_THROTTLE_HIGH   // was accelerating (e.g. >40%)
  AND throttle_now   < TURBO_THROTTLE_LOW    // now lifted off    (e.g. <10%)
  AND rpm_now        > TURBO_RPM_MIN         // in boost range    (e.g. >1500)
  AND current_gear  <= TURBO_MAX_GEAR        // 1st or 2nd gear
  AND time_since_last_turbo > TURBO_COOLDOWN): // not already playing
      set volume: gear 1 → 30 (100%), gear 2 → 21 (70%)
      play Turbo sound (/mp3/0001.mp3)
      record timestamp
```

### Tunable thresholds (constants in code)

| Constant              | Default | Meaning                                      |
| --------------------- | ------- | -------------------------------------------- |
| `TURBO_THROTTLE_HIGH` | 40 %    | Throttle must have been above this           |
| `TURBO_THROTTLE_LOW`  | 10 %    | Throttle must now be below this              |
| `TURBO_RPM_MIN`       | 1500    | Minimum RPM to trigger (in boost)            |
| `TURBO_MAX_GEAR`      | 2       | Only trigger in gears ≤ this                 |
| `TURBO_COOLDOWN_MS`   | 2000 ms | Minimum gap between consecutive Turbo sounds |
| `TURBO_VOLUME_GEAR1`  | 30      | DFPlayer volume (0–30) for 1st gear trigger  |
| `TURBO_VOLUME_GEAR2`  | 21      | DFPlayer volume (0–30) for 2nd gear trigger  |

These can be adjusted to taste once tested in the car.

---

## 3. Bill of materials

| Item                             | Approx price (UK) | Status   | Notes                                                         |
| -------------------------------- | ----------------- | -------- | ------------------------------------------------------------- |
| ELEGOO ESP-WROOM-32 DevKit       | £6–8              | To order | Bluetooth Classic + BLE + WiFi. [Amazon link][amz-esp32]      |
| 0.96" SSD1306 OLED (128x64, SPI) | —                 | Have it  | Pins: GND,VCC,D0,D1,RES,DC,CS — SPI mode                      |
| KY-040 rotary encoder module     | —                 | Have it  | 5-pin: CLK / DT / SW / + / GND                                |
| DFPlayer Mini MP3 module         | —                 | Have it  | Same module used in [arduino-laser-target][laser-target]      |
| microSD card (≤32 GB, FAT32)     | —                 | Check    | Must be formatted FAT32; Turbo .mp3 file goes in /mp3/ folder |
| Small speaker (4–8 Ω, 0.5–3 W)   | £2–3              | Check    | Connects to DFPlayer Mini's built-in 3W amp (SPK1/SPK2)       |
| Breadboard + jumper wires        | —                 | Have it  |                                                               |
| Micro-USB cable                  | —                 | Have it  | Power + flashing                                              |
| OBD BLE dongle                   | —                 | Have it  | **BLE** (not Classic BT). Name: "OBD BLE", service FFF0.     |
| **Total new spend**              | **~£10**          |          | ESP32 + speaker (if needed)                                   |

### Hardware decisions (from planning session)

- **ESP8266 rejected** — no Bluetooth at all.
- **ESP32-C3 SuperMini rejected** — BLE only, no Bluetooth Classic.
- **ELEGOO ESP-WROOM-32 chosen** — Bluetooth 4.2 Classic + BLE.
- **KY-040 rotary encoder** replaces original 3-button design.
- **0.96" SSD1306 OLED** confirmed (model ep0096dtan001a).
- **OBD dongle confirmed BLE** — device name "OBD BLE", PHY LE 1M, service FFF0.
  iOS app (CarScanner) proved it: Apple blocks Classic BT for third-party apps, so any
  dongle that works with an iPhone app is definitively BLE. Classic BT code (BluetoothSerial)
  cannot see BLE devices at all; switched to ESP32 BLE stack (BLEDevice.h).
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
│  ┌─────────────┐                                     │
│  │ OBD Service │                                     │
│  │ (PID poller)│                                     │
│  └─────┬───────┘                                     │
│        │                                            │
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

- **BLE (Bluetooth Low Energy)** — the "OBD BLE" dongle is BLE, not Classic BT.
  Uses ESP32's `BLEDevice` / `BLEClient` stack. Commands go to write characteristic
  FFF2; responses arrive via notify callbacks on FFF1.
- **Auto-connect** — BLE scan finds any device advertising service FFF0 or whose
  name contains "OBD", "ELM", or "LINK"; connects without user input.
- **Turbo trigger is event-driven** — checked every OBD poll cycle (100 ms);
  compares current vs previous throttle reading.
- **DFPlayer Mini** — standalone MP3 module with its own SD card; the ESP32
  sends a single UART command to trigger playback. No audio processing on
  the ESP32 itself.
- **KY-040 encoder** — CLK/DT read manually; SW debounced with Bounce2 (same
  pattern as [arduino-laser-target][laser-target]).

---

## 5. Wiring

**Wiring table — to be confirmed after breadboard placement is verified.**

> **Breadboard internal connections:** all 5 holes in the same row and same
> bank (J–F or E–A) are electrically connected. A module's pins must each go
> into a **different row** of the same column so they stay isolated.
>
> **Power rails** run vertically on the **left side** (beside col J) and
> **right side** (beside col A), each with a + and a − strip, row 1–60.
>
> **ESP32 occupies the full breadboard width:** left pins col J, right pins
> col A (rows 3–17). All other components go in rows 18+.

### Breadboard component placement

Columns run left → right: J I H G F | centre gap | E D C B A  
Left power rail beside J. Right power rail beside A.  
ESP32 spans cols **I to A** (9 columns). Col J is empty, beside the left power rail.

ELEGOO ESP-WROOM-32 — rotated 180°. USB hangs off above row 5. 3V3/VIN at top (rows 5–19).

| Row | Col I (left side) | Col A (right side) |
| --- | ----------------- | ------------------ |
| 5   | 3V3               | VIN                |
| 6   | GND               | GND                |
| 7   | IO15              | IO13               |
| 8   | IO2               | IO12               |
| 9   | IO4               | IO14               |
| 10  | IO16 (RX2)        | IO27               |
| 11  | IO17 (TX2)        | IO26               |
| 12  | IO5               | IO25               |
| 13  | IO18 (SCK)        | IO33               |
| 14  | IO19              | IO32               |
| 15  | IO21              | IO35               |
| 16  | IO3 (RX0)         | IO34               |
| 17  | IO1 (TX0)         | IO39               |
| 18  | IO22              | IO36               |
| 19  | IO23 (MOSI)       | EN                 |

```
  [left+][-]  J    I    H    G    F  │  E    D    C    B    A  [-][+right]
 ──────────────────────────────────────────────────────────────────────────
  1          .    .    .    .    .  │  .    .    .    .    .
  2          .    .    .    .    .  │  .    .    .    .    .
  3          .    .    .    .    .  │  .    .    .    .    .
  4          .    .    .    .    .  │  .    .    .    .    .
  5          . [3V3]   .    .    .  │  .    .    .    .  [VIN]   ← USB above
  6          . [GND]   .    .    .  │  .    .    .    .  [GND]
  7          . [IO15]  .    .    .  │  .    .    .    .  [IO13]
  8          . [IO2]   .    .    .  │  .    .    .    .  [IO12]
  9          . [IO4]   .    .    .  │  .    .    .    .  [IO14]
 10          . [IO16]  .    .    .  │  .    .    .    .  [IO27]
 11          . [IO17]  .    .    .  │  .    .    .    .  [IO26]
 12          . [IO5]   .    .    .  │  .    .    .    .  [IO25]
 13          . [IO18]  .    .    .  │  .    .    .    .  [IO33]
 14          . [IO19]  .    .    .  │  .    .    .    .  [IO32]
 15          . [IO21]  .    .    .  │  .    .    .    .  [IO35]
 16          . [IO3]   .    .    .  │  .    .    .    .  [IO34]
 17          . [IO1]   .    .    .  │  .    .    .    .  [IO39]
 18          . [IO22]  .    .    .  │  .    .    .    .  [IO36]
 19          . [IO23]  .    .    .  │  .    .    .    .   [EN]
 20          .    .    .    .    .  │  .    .    .    .    .
 21          .    .    .    .    .  │  .    .    .    .    .
 22          .    .    .    .    .  │  .    .    .    .    .
 23          .    .    .    .    .  │  .    .    .    .    .
 24          .    .    .    .    .  │  .    .    .    .    .
 25          .    .    .    .    .  │  .    .    .    .    .
 26          .    .    .    .    .  │  .    .    .    .    .
 27          .    .    .    .    .  │  .    .    .    .    .
 28          .    .    .    .    .  │  .    .    .    .    .
 29          .    .    .    .    .  │  .    .    .    .    .
 30          .    .    .    .    .  │  [GND].    .    .    .    ← Encoder
 31          .    .    .    .    .  │  [+]  .    .    .    .
 32          .    .    .    .    .  │  [SW] .    .    .    .
 33          .    .    .    .    .  │  [DT] .    .    .    .
 34          .    .    .    .    .  │  [CLK].    .    .    .    ← Encoder
 35          .    .    .    .    .  │  .    .    .    .    .
 36          .    .    .    .    .  │  .    .    .    .    .
 37          .    .    .    .    .  │  .    .    .    .    .
 38          .    .    .    .    .  │  .    .    .    .    .
 39          .    .    .    .    .  │  .    .    .    .    .
 40          .    .    .    .    .  │  .    .    .    .    .
 41          .    .    .    .    .  │  .    .    .    .    .
 42          .    .    .    . [GND] │  .    .    .    .    .    ← OLED pin 1
 43          .    .    .    . [VCC] │  .    .    .    .    .
 44          .    .    .    . [SCK] │  .    .    .    .    .
 45          .    .    .    .[MOSI] │  .    .    .    .    .
 46          .    .    .    . [RES] │  .    .    .    .    .
 47          .    .    .    .  [DC] │  .    .    .    .    .
 48          .    .    .    .  [CS] │  .    .    .    .    .    ← OLED pin 7
 49          .    .    .    .    .  │  .    .    .    .    .
 50          .    .    .    .    .  │  .    .    .    .    .
 51          .    .    .    .    .  │  .    .    .    .    .
 52          . [VCC]   .    .    .  │  .    .    .    .  [BUSY]   ← DFPlayer
 53          .  [RX]   .    .    .  │  .    .    .    .  [USB-]
 54          .  [TX]   .    .    .  │  .    .    .    .  [USB+]
 55          .[DAC_R]  .    .    .  │  .    .    .    . [ADKEY2]
 56          .[DAC_I]  .    .    .  │  .    .    .    . [ADKEY1]
 57          .[SPK_1]  .    .    .  │  .    .    .    .  [IO_2]
 58          . [GND]   .    .    .  │  .    .    .    .  [GND]
 59          .[SPK_2]  .    .    .  │  .    .    .    .  [IO_1]
 60          .    .    .    .    .  │  .    .    .    .    .
 ──────────────────────────────────────────────────────────────────────────
```

**Component notes:**

- OLED 7-pin header plugs into **col F, rows 42–48** (one pin per row):
  GND=F42, VCC=F43, SCK=F44, MOSI=F45, RES=F46, DC=F47, CS=F48.
- DFPlayer Mini straddles the full breadboard width: left pins in **col I rows 52–59**,
  right pins in **col A rows 52–59**. Module matches datasheet orientation:
  VCC/BUSY at row 52, SPK_2/IO_1 at row 59.
- KY-040 encoder 5-pin header plugs into **col E, rows 30–34** (one pin per row):
  GND=E30, +=E31, SW=E32, DT=E33, CLK=E34.

---

### Wiring

**Power rails**

| From                | To                   | Voltage | Note       |
| ------------------- | -------------------- | ------- | ---------- |
| J5 (=I5, ESP32 3V3) | Left rail (+) row 5  | 3.3 V   | Short wire |
| J6 (=I6, ESP32 GND) | Left rail (−) row 6  | GND     | Short wire |
| A5 (ESP32 VIN)      | Right rail (+) row 5 | 5 V     | Short wire |
| A6 (ESP32 GND)      | Right rail (−) row 6 | GND     | Short wire |

**Encoder (col E, rows 30–34)**

| Signal     | From | To                   | Route                               | ~Length |
| ---------- | ---- | -------------------- | ----------------------------------- | ------- |
| GND        | E30  | Right rail (−)       | Short right-bank wire               | 2 cm    |
| + (3V3)    | E31  | Left rail (+) row 31 | Full-board crossing via row 31      | 5 cm    |
| SW → IO27  | E32  | A10                  | Right bank, col E→A, routing upward | 8 cm    |
| DT → IO26  | E33  | A11                  | Right bank, col E→A, routing upward | 8 cm    |
| CLK → IO25 | E34  | A12                  | Right bank, col E→A, routing upward | 8 cm    |

**OLED (col F, rows 42–48)**

| Signal      | From | To            | Route                                        | ~Length |
| ----------- | ---- | ------------- | -------------------------------------------- | ------- |
| GND         | F42  | Left rail (−) | Short left-bank wire                         | 2 cm    |
| VCC (3V3)   | F43  | Left rail (+) | Short left-bank wire                         | 2 cm    |
| SCK → IO18  | F44  | J13           | Left bank, cols F→J, routing upward          | 11 cm   |
| MOSI → IO23 | F45  | J19           | Left bank, cols F→J, routing upward          | 10 cm   |
| RES → IO15  | F46  | J7            | Left bank, cols F→J, routing upward          | 13 cm   |
| DC → IO32   | F47  | A14           | Full-board cross: left bank F → right bank A | 15 cm   |
| CS → IO5    | F48  | J12           | Left bank, cols F→J, routing upward          | 13 cm   |

**DFPlayer Mini (col I, rows 52–59)**

| Signal      | From | To                   | Route                                          | ~Length |
| ----------- | ---- | -------------------- | ---------------------------------------------- | ------- |
| VCC (3V3)   | I52  | Left rail (+)        | Short wire via J52                             | 2 cm    |
| RX → IO17   | I53  | H36 (resistor leg 2) | Left bank, routing upward — see resistor below | 6 cm    |
| TX → IO16   | I54  | J10                  | Left bank, cols I→J, routing upward            | 14 cm   |
| GND         | I58  | Left rail (−)        | Short wire via J58                             | 2 cm    |
| SPK_1       | I57  | Mini-amp             | External                                       | —       |
| SPK_2       | I59  | Mini-amp             | External                                       | —       |
| GND (right) | A58  | Right rail (−)       | Short right-bank wire                          | 2 cm    |

**1 kΩ resistor — DFPlayer RX line (placed at col H, rows 35–36)**

| Leg | Hole | Connected to                         |
| --- | ---- | ------------------------------------ |
| 1   | H35  | J11 via wire (=I11=IO17/TX2) — ~8 cm |
| 2   | H36  | I53 via wire (DFPlayer RX) — ~6 cm   |

The resistor sits vertically in col H bridging rows 35→36. It stays on the left bank, in the free area between encoder (row 34) and OLED (row 42).

---

## 6. Software stack

### Toolchain

- **Arduino IDE 2.x**
- ESP32 board support:
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Board: **ESP32 Dev Module**

### Libraries (Library Manager)

| Library               | Purpose                                                                                                        |
| --------------------- | -------------------------------------------------------------------------------------------------------------- |
| `ESP32 BLE Arduino`   | BLE client — `BLEDevice.h`, `BLEClient.h`, `BLEScan.h` (built into ESP32 core)                                |
| `U8g2`                | OLED — real device: `U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI`; Wokwi sim: `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` |
| `DFRobotDFPlayerMini` | DFPlayer Mini MP3 module driver                                                                                |
| `Bounce2`             | KY-040 button debounce                                                                                         |

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

### Live queries — three-tier polling strategy

OBD2 polling rate adapts to engine state to avoid unnecessary blocking
and keep the rotary encoder responsive at all times.

| Mode        | RPM range | PIDs polled                                      | Rate         | Purpose                                        |
| ----------- | --------- | ------------------------------------------------ | ------------ | ---------------------------------------------- |
| **Parked**  | < 200     | `ATRV` (battery), `0105` (coolant), `010C` (RPM) | every 3 s    | Engine off — show parked info screen           |
| **Idle**    | 200–999   | `010C` (RPM)                                     | every 500 ms | Engine running, not moving — no Turbo possible |
| **Driving** | ≥ 1000    | `0111` (TPS), `010D` (speed), `010C` (RPM)       | every 100 ms | Turbo trigger active                           |

**Encoder priority:** when the rotary encoder is turned, OBD2 polling is
suspended for 500 ms (`ENCODER_PRIORITY_MS`). The main loop runs freely
during this window so the display updates instantly on every detent.

### PID reference

| PID     | Command  | Response   | Formula              | Range      |
| ------- | -------- | ---------- | -------------------- | ---------- |
| TPS     | `0111\r` | `4111XX`   | `XX × 100 / 255`     | 0–100 %    |
| Speed   | `010D\r` | `410DXX`   | `XX`                 | 0–255 km/h |
| RPM     | `010C\r` | `410CXXYY` | `(XX×256+YY) / 4`    | 0–16383    |
| Coolant | `0105\r` | `4105XX`   | `XX − 40`            | −40–215 °C |
| Battery | `ATRV\r` | `"12.4V"`  | parse float, V range | 6–16 V     |

### Gear calculation

OBD2 does not expose gear directly. Estimated from the RPM ÷ speed ratio
inside `estimateGear()`. Approximate thresholds for the Mercedes CLA180:

| Ratio (RPM ÷ km/h) | Gear |
| ------------------ | ---- |
| > 110              | 1    |
| > 65               | 2    |
| > 43               | 3    |
| > 30               | 4    |
| > 22               | 5    |
| ≤ 22               | 6    |

Calibrate by driving steadily in each gear and logging the ratio from the
serial monitor (`[Turbo]` lines show gear at trigger time).

---

## 8. State machine (UI flow)

```
[BOOT]
  │
  ▼
[SCANNING]   ── auto-scans BT, spinner on OLED
  │               connects automatically on ELM/OBD/LINK name match
  ▼
[CONNECTING] ── "Found: <name> / Connecting…"
  │               failure → SCANNING
  ▼
[INIT_ELM]   ── AT command sequence (ATZ, ATE0, ATL0, ATS0, ATH0, ATSP0)
  │               reads battery voltage, shows "OBD2 connected!" screen
  │               failure → SCANNING
  ▼
[RUNNING]    ── polling mode determined by RPM:
  │
  ├─ RPM < 200   (PARKED)
  │    poll: battery + coolant + RPM every 3 s
  │    display: parked info screen (battery V, coolant °C, "Start engine…")
  │
  ├─ RPM 200–999 (IDLE)
  │    poll: RPM only every 500 ms
  │    display: 4 gauge views (rotary fully responsive)
  │
  └─ RPM ≥ 1000  (DRIVING)
       poll: TPS + speed + RPM every 100 ms
       Turbo trigger active
       encoder priority: OBD2 pauses 500 ms on encoder turn
       display: 4 gauge views

Encoder (in all RUNNING sub-states):
  rotate → cycle 4 views: Throttle / Speed / All metrics / Dual bars
  click  → disconnect Bluetooth, return to SCANNING
```

---

## 9. Polling loop timing

```
PARKED  (RPM < 200):
    every 3000 ms : poll ATRV + 0105 + 010C  (~900 ms total blocking)
    every  200 ms : redraw parked screen

IDLE    (RPM 200–999):
    every  500 ms : poll 010C only            (~300 ms total blocking)
    every   50 ms : redraw gauge screen

DRIVING (RPM ≥ 1000):
    every  100 ms : poll 0111 + 010D + 010C   (~900 ms total blocking)
                    → checkTurbo() after each full poll
    every   50 ms : redraw gauge screen

Encoder priority (all modes):
    on encoder turn : skip OBD2 poll for 500 ms
                      redraw runs freely at full speed

obdSend() timeout : 300 ms (healthy CAN/BT responses arrive < 150 ms)
```

Each `obdSend()` call blocks the CPU until the ELM327 sends `>` or the
timeout expires. There is no RTOS — cooperative scheduling via `millis()`
timers. The encoder ISR fires independently of the polling loop so no
encoder turns are lost, even mid-poll.

---

## 10. Build plan — phased

### Phase 1: Hardware verification (day 1)

- [ ] Flash "Blink" — confirm ESP32 toolchain
- [ ] Wire OLED, run `oled_test.ino` — confirm SSD1306 displays counter
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

| Risk                                     | Mitigation                                                                                                         |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Cheap ELM327 clones have buggy firmware  | Test `ATZ` early — should return `ELM327 v1.5`. If garbage, swap the dongle.                                       |
| BLE dongle only connectable by one device at a time | Disconnect from any phone app before the ESP32 tries to connect.          |
| Some cars don't support some PIDs        | Send `0100` first — bitmap of supported PIDs. Only poll supported ones.                                            |
| Polling too fast → `NO DATA` responses   | Cap at 10 Hz total across 3 PIDs. Use `ATAT1` for adaptive timing.                                                 |
| `\r` vs `\r\n` mismatch with ELM327      | Always send `\r` only; wait for `>` before sending next command.                                                   |
| OLED flicker                             | Use U8g2 full-buffer mode (`_F_`).                                                                                 |
| ESP32 brown-out during engine crank      | Power from a buffered USB socket; add capacitor on 5V rail.                                                        |
| ATZ takes up to 3 s                      | Use 3000 ms timeout for ATZ; 1000 ms for all other AT commands.                                                    |
| DFPlayer busy / sound cut short          | Check BUSY pin (DFPlayer GPIO 16) before triggering next sound; respect cooldown timer.                            |
| Turbo triggers too eagerly or not enough | Tune the four `TURBO_*` threshold constants after first real-car test.                                             |
| G-force shows ~1 g at rest               | Subtract gravity (≈9.81 m/s²) from Z-axis before display.                                                          |
| Manual gearbox — no clutch PID on OBD2   | Throttle-drop detection is the trigger (driver lifts off when pressing clutch). No need to detect clutch directly. |

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

- **ESP32 BLE Arduino library:** https://github.com/espressif/arduino-esp32/tree/master/libraries/BLE
- **ELEGOO ESP-WROOM-32 (chosen board):** https://www.amazon.co.uk/ELEGOO-ESP-WROOM-32-Development-Micro-USB-Microcontroller/dp/B0D8T5P8JM
- **ELEGOO ESP32 official wiki (pinout):** https://wiki.elegoo.com/oshw-getting-started-kits/first-look-esp32
- **ESP32-WROOM-32 pinout reference:** https://lastminuteengineers.com/esp32-wroom-32-pinout-reference/
- **DOIT ESP32 DevKit V1 pinout diagram:** https://www.circuitstate.com/pinouts/doit-esp32-devkit-v1-wifi-development-board-pinout-diagram-and-reference/
- **ESP32-C3 SuperMini (rejected — BLE only):** https://www.amazon.co.uk/Youmile-Development-SuperMini-Bluetooth-Frequency/dp/B0D2WSYRZM

### Display, audio, encoder

- **U8g2 graphics library:** https://github.com/olikraus/u8g2/wiki
- **DFPlayer Mini wiki (pinout & API):** https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299
- **DFPlayer Mini pinout & protocol reference:** https://wiki.dfrobot.com/dfr0299/docs/20906
- **DFPlayer Mini wiring diagrams (mono/stereo):** https://circuitjournal.com/how-to-use-the-dfplayer-mini-mp3-module-with-an-arduino

### Reference projects

- **arduino-laser-target** (same SSD1306 + KY-040 + DFPlayer Mini pattern):
  https://github.com/marcelovani/arduino-laser-target

### Testing tools

Four layers — run in order, see the Testing section in README.md for full commands.

| Layer            | Command                      | What it checks                                                 | Hardware needed |
| ---------------- | ---------------------------- | -------------------------------------------------------------- | --------------- |
| Unit tests       | `make test-unit`             | parse_pid, estimate_gear, TurboTrigger logic, all scenarios    | None            |
| Visual monitor   | `make scenario`              | Trigger timing and sequence, threshold tuning                  | None            |
| Wokwi simulation | `make wokwi-build` + VS Code | Compiled firmware, OLED, encoder, buzzer (GPIO17), LED (GPIO4) | None            |
| Serial Monitor   | Flash → Arduino IDE          | Full OBD2 + BT stack, real triggers, gear calibration          | ESP32 via USB   |

- **ELM327 emulator (integration tests without a car):** https://github.com/Ircama/ELM327-emulator
  — used by `make test-emulator` via `tests/integration/mock_elm327.py`
- **Python test mirror:** `tests/obd_logic.py` mirrors all C++ logic; keep in sync when changing constants
- **Gear calibration:** drive in each gear at steady speed, note `[OBD] RPM=... Speed=...` in Serial Monitor,
  compute ratio = RPM ÷ km/h, update thresholds in `estimateGear()` and `tests/obd_logic.py`

---

## 13. Code skeleton

```cpp
// arduino-obd2-turbo — main sketch
// Libraries: ESP32 BLE (built-in), U8g2, DFRobotDFPlayerMini, Bounce2

#include <BLEDevice.h>
#include <U8g2lib.h>
#include <DFRobotDFPlayerMini.h>
#include <Bounce2.h>

// ── Pins ─────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27

#ifdef SIMULATION
  #define PIN_BUZZER  17   // passive buzzer — beeps 900 Hz / 350 ms on Turbo fire
  #define PIN_LED      4   // red LED — blinks 1 s after each Turbo fire
#else
  #define PIN_DFP_RX  16   // ESP32 RX2 ← DFPlayer TX
  #define PIN_DFP_TX  17   // ESP32 TX2 → DFPlayer RX
#endif

// ── Turbo thresholds (tune after first car test) ────────────────────────────
#define TURBO_THROTTLE_HIGH  40.0f   // % — was accelerating
#define TURBO_THROTTLE_LOW   10.0f   // % — now lifted off
#define TURBO_RPM_MIN        1500.0f // RPM — in boost range
#define TURBO_MAX_GEAR       2       // only gears 1 and 2
#define TURBO_COOLDOWN_MS    2000    // ms between sounds

// ── Objects ───────────────────────────────────────────────────────────────
BluetoothSerial BT;
// Real device: SPI OLED (SCK=18, MOSI=23, RES=15, DC=32, CS=5)
U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI oled(U8G2_R0, /*cs=*/5, /*dc=*/32, /*reset=*/15);
DFRobotDFPlayerMini dfplayer;
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

    oled.begin();
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

## 14. Next steps / future work

See [gear_detection_plan.md](gear_detection_plan.md) for:
- Current implementation status (sketch modules, build modes, resolved bugs)
- Gear detection research: why no standard OBD2 PID exists for clutch/gear on a manual CLA180
- Future improvement options: speed-band tuning from recordings, Engine Load heuristic, CAN bus sniffer

[amz-esp32]: https://www.amazon.co.uk/ELEGOO-ESP-WROOM-32-Development-Micro-USB-Microcontroller/dp/B0D8T5P8JM
[elm-home]: https://www.elm327.com/index.php
[laser-target]: https://github.com/marcelovani/arduino-laser-target

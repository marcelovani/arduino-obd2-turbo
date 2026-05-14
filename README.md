# Arduino OBD2 Turbo Sound Emulator

An ESP32 device that reads live OBD2 data from your car and plays a turbo
blow-off valve "pssssh" sound through a speaker whenever you lift off
the throttle during a gear change — emulating the iconic Fast & Furious
turbo sound.

The device also shows live gauges (throttle, RPM, speed, gear) on a
small OLED screen.

---

## How it works

A turbo blow-off valve vents pressurised intake air when the driver lifts
off the throttle while the turbo is still spinning. This creates the
distinctive "pssssh" sound. This device detects that moment from OBD2 data:

1. Connects to the car's OBD2 port via a Bluetooth ELM327 dongle
2. Polls OBD2 data at a rate that adapts to engine state:
   - **Parked** (RPM < 200) — reads battery voltage, coolant temp, RPM every 3 s
   - **Idle** (RPM 200–999) — reads RPM only every 500 ms; rotary encoder fully responsive
   - **Driving** (RPM ≥ 1000) — reads throttle, speed, RPM every 100 ms
3. When the rotary encoder is turned, OBD2 polling pauses for 500 ms so the screen updates instantly
4. In driving mode, when throttle drops rapidly from high to low while RPM is in the
   boost range (and in 1st or 2nd gear), it triggers a pre-recorded Turbo sound
5. Displays live gauges on a 0.96" OLED screen — 4 views navigated with the rotary encoder

**Target car:** Mercedes CLA180 (2011, gasoline)

---

## Hardware

| Component             | Notes                                                   |
| --------------------- | ------------------------------------------------------- |
| ELEGOO ESP-WROOM-32   | Main microcontroller — Bluetooth Classic + BLE          |
| OBD BLE dongle        | BLE (not Classic BT) — plugs into car OBD2 port         |
| 0.96" SSD1306 OLED    | 128×64 SPI display (pins: GND,VCC,D0,D1,RES,DC,CS)      |
| DFPlayer Mini         | MP3 playback module — 3.3V power, 1kΩ on RX line        |
| KY-040 rotary encoder | Navigation: rotate = cycle views, click = disconnect    |
| microSD card (FAT32)  | Stores audio files in `/mp3/` — see Audio files section |
| Small speaker (4–8 Ω) | Plays the Turbo sound                                   |

---

## Audio files (SD card)

The `mp3/` folder in this repo contains the source MP3 files. Copy the entire
`mp3/` folder to the **root** of the microSD card as-is — no renaming needed.
The sketch uses `playMp3Folder(n)` which looks up files by name, so copy order
does not matter.

| Filename   | Content              | When played                                     |
| ---------- | -------------------- | ----------------------------------------------- |
| `0001.mp3` | "Pairing"            | Each time the device starts scanning for ELM327 |
| `0004.mp3` | "OBD2 not connected" | Scan timeout (30 s) or Bluetooth connect fail   |
| `0008.mp3` | "Demo mode"          | When demo mode is turned on via the menu        |
| `0009.mp3` | "Goodbye"            | When the Power option is used to turn off       |
| `0010.mp3` | Long spray           | Turbo trigger in 1st gear (1st → 2nd change)    |
| `0011.mp3` | Faster spray         | Turbo trigger in 2nd gear (2nd → 3rd change)    |

Format the card as **FAT32**. The `mp3/` folder must be at the root of the card.

### Required MP3 format

DFPlayer Mini (and its clones) are picky about file format. Files outside this
spec will play distorted, play silently, or not play at all:

| Property     | Required value          | Notes                                          |
| ------------ | ----------------------- | ---------------------------------------------- |
| MPEG version | MPEG-1 Layer III        | MPEG-2 / MPEG-2.5 cause distortion on clones   |
| Sample rate  | 44100 Hz                | Other rates work but 44.1 kHz is most reliable |
| Bit rate     | 128 kbps CBR            | VBR causes early cutoff or stuttering          |
| Channels     | Mono                    | Stereo works but wastes space for one speaker  |
| ID3 tags     | None (strip completely) | ID3v2 headers at file start cause glitches     |

Re-encode any non-conforming file with ffmpeg:

```bash
ffmpeg -i input.mp3 \
  -ar 44100 -ac 1 -b:a 128k \
  -f mp3 -id3v2_version 0 -write_id3v1 0 \
  output.mp3
```

The files `10.mp3` and `11.mp3` in this repo are currently MPEG-2/24 kHz and
need re-encoding before copying to the card.

Voice clips were generated with [MiniMax Text-to-Speech](https://www.minimax.io/audio/text-to-speech).

### Speaker wiring

Connect the speaker **between SPK1 and SPK2** — both pins are live outputs from
the built-in BTL amplifier. Do not connect either pin to GND; grounding either
output shorts the amplifier and can damage the module.

```
  SPK1 ──── wire 1 ──┐
                     🔊
  SPK2 ──── wire 2 ──┘
```

### Idle hiss

The DFPlayer Mini's onboard amp stays active between tracks and amplifies
power-rail noise as an audible hiss. Fixes in order of effectiveness:

1. **100 µF capacitor across VCC/GND** (most effective) — the ESP32's 3.3 V
   pin is noisy; a decoupling cap on the DFPlayer's power pins eliminates most
   of the hiss.
2. **`dfplayer.stop()` after playback** — on some clones this puts the amp in a
   lower-noise idle state; costs nothing in hardware.
3. **10–22 Ω resistors in series with each speaker wire** — passive low-pass
   filter that trims high-frequency hiss at a small volume cost.

---

## Turbo trigger logic

```
throttle was > 40%  AND  throttle now < 10%
AND  RPM > 1500  AND  gear <= 2
→ play /mp3/0001.mp3
```

All thresholds are constants at the top of the sketch — tune them after
your first real-car test.

---

## Software

### Sketch

`sketches/turbo/turbo.ino` — single source file with three build modes
selected by compile-time flags:

| Flag           | Command            | Hardware                                   | OBD2                 | Audio              | Scenario                  |
| -------------- | ------------------ | ------------------------------------------ | -------------------- | ------------------ | ------------------------- |
| `-DSIMULATION` | `make wokwi-build` | Wokwi (I2C OLED, buzzer GPIO17, LED GPIO4) | No                   | 900 Hz buzzer beep | Built-in 24 s loop (auto) |
| `-DDEMO`       | `make demo-upload` | Real ESP32 (SPI OLED, DFPlayer, speaker)   | No                   | MP3 via DFPlayer   | Built-in 24 s loop (auto) |
| _(none)_       | `make deploy`      | Real ESP32                                 | Yes — BLE OBD dongle | MP3 via DFPlayer   | Live OBD2 data            |

The built-in scenario fires two Turbo triggers per loop: one during a
1st→2nd gear change at ~9.5 s and one during a 2nd→3rd gear change at ~12.5 s.
Encoder button resets the counter (simulation) or restarts the scenario (demo).

### Arduino IDE (for flashing to the real device)

Download **[Arduino IDE 2.x](https://www.arduino.cc/en/software)** and install
the ESP32 board package:

1. Open Arduino IDE → **File → Preferences**
2. Add to _Additional boards manager URLs_:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. **Tools → Board → Boards Manager** → search `esp32` → install **esp32 by Espressif**
4. Select board: **Tools → Board → ESP32 Dev Module**

### arduino-cli (for command-line builds and Wokwi firmware)

Required for `make wokwi-build` and `make wokwi-setup`.

```bash
# Mac
brew install arduino-cli

# Linux
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

Then run once to install the ESP32 board package and required libraries:

```bash
make wokwi-setup
```

### Arduino libraries

| Library               | Install via                                            |
| --------------------- | ------------------------------------------------------ |
| `U8g2`                | Arduino IDE Library Manager                            |
| `DFRobotDFPlayerMini` | Arduino IDE Library Manager                            |
| `Bounce2`             | Arduino IDE Library Manager                            |
| `ESP32 BLE Arduino`   | Built-in (part of ESP32 board package — `BLEDevice.h`) |

`make wokwi-setup` installs `U8g2` and `Bounce2` via arduino-cli automatically.

### Python (for tests and scenario monitor)

Python 3.x is required for the test suite and visual monitor.

```bash
make build    # creates .venv and installs all Python dependencies
make test     # run all tests
```

---

## Wokwi simulation

A browser-based circuit simulation lives in [`Emulators/Wokwi/`](Emulators/Wokwi/).
It replays a built-in driving scenario that fires two Turbo triggers, shows live
gauges on the simulated OLED, and lets the encoder cycle views — no hardware needed.

Simulated hardware (replaces real-device peripherals under `#ifdef SIMULATION`):

| Component      | Pin             | Role                                                                  |
| -------------- | --------------- | --------------------------------------------------------------------- |
| SSD1306 OLED   | I2C (GPIO21/22) | Same display, but I2C — Wokwi's SSD1306 component only supports I2C   |
| KY-040 encoder | GPIO25/26/27    | Same as real device                                                   |
| Passive buzzer | GPIO17 (TX2)    | Plays 900 Hz beep for 350 ms when Turbo fires (replaces DFPlayer MP3) |
| Red LED        | GPIO4           | Blinks for 1 s after each Turbo fire (visual indicator)               |

**Option A — wokwi.com (browser, no compilation needed)**

1. Go to [wokwi.com](https://wokwi.com/projects/463843555678407681) and create a new ESP32 project
2. Copy `diagram.json`, `libraries.txt` from `Emulators/Wokwi/` and
   `sketches/turbo/turbo.ino` into the project (rename to `sketch.ino`)
3. In the wokwi.com editor, add `-DSIMULATION` to the compile flags
4. Press **Play**

**Option B — VS Code / Windsurf extension**

See the full setup guide at [docs.wokwi.com/vscode/getting-started](https://docs.wokwi.com/vscode/getting-started).
The extension is recommended via `.vscode/extensions.json`.

1. Install the **Wokwi for VS Code** extension from the marketplace
2. Press **F1 → Wokwi: Request a new License**, then click **GET YOUR LICENSE**
   on the Wokwi website and approve the browser prompt — a free account is enough
3. Compile the sketch (firmware must exist before the simulator can run):

```bash
brew install arduino-cli          # Mac — one time
make wokwi-setup                  # installs ESP32 board + libraries — one time
make wokwi-build                  # compiles sketch → Emulators/Wokwi/build/
```

4. Open `Emulators/Wokwi/diagram.json` in VS Code and press **F1 → Wokwi: Start Simulator**

---

## Testing

There are four testing layers, from fastest to most complete. Run them in
order when you change trigger logic, thresholds, or the driving scenario.

### 1. Unit tests — no hardware needed

Verifies all trigger logic (parse_pid, estimate_gear, TurboTrigger) and
replays every driving scenario to check the expected trigger count.

```bash
make build        # first time only — creates Python venv
make test-unit
```

All 39 tests run in under a second. Run this whenever you change a threshold
constant in the sketch **and** its mirror in `tests/obd_logic.py`.

### 2. Visual scenario monitor — no hardware needed

Replays driving scenarios in the terminal in real time, printing each data
point and flagging when Turbo fires. Good for checking trigger timing and
sequence before touching the hardware.

```bash
make scenario                                    # all scenarios
make scenario SCENARIO=first_gear_change         # one scenario
make scenario THROTTLE_HIGH=35 RPM_MIN=1200      # tune thresholds
```

### 3. Wokwi simulation — ESP32 firmware, no car

Runs the compiled sketch in a browser-based circuit simulation with a virtual
OLED, encoder, buzzer, and LED. The `#ifdef SIMULATION` code path replays the
built-in driving scenario and fires two Turbo triggers on the virtual display.
See the [Wokwi simulation](#wokwi-simulation) section above for setup.

```bash
make wokwi-build   # compile first, then open diagram.json in VS Code
```

### 4. Real ESP32 via USB — Serial Monitor

Flash the sketch to the ESP32, open the Serial Monitor at **115200 baud**, and
watch the OBD2 poll output:

```
[OBD] TPS=87.0 RPM=3400 Speed=60
[OBD] TPS=4.0  RPM=3300 Speed=60
[Turbo] gear=1 prev_tps=87.0 tps=4.0 rpm=3300
```

Every `[Turbo]` line confirms a trigger. This is also where you collect
real RPM/speed pairs at steady cruise to calibrate the gear ratio thresholds
in `TURBO_*` constants (see the calibration note in `tests/obd_logic.py`).

**Option A — [Arduino IDE 2.x](https://www.arduino.cc/en/software) (recommended)**

1. Connect ESP32 via USB
2. Arduino IDE → Tools → Board → **ESP32 Dev Module**
3. Tools → Port → `/dev/cu.usbserial-...` (Mac) or `COM...` (Windows)
4. Upload sketch, then open Serial Monitor (Ctrl+Shift+M) at **115200 baud**

**Option B — `make deploy` (command line)**

```bash
make deploy                              # auto-detects /dev/cu.usbserial-* or /dev/cu.SLAB_USBtoUART*
make deploy PORT=/dev/cu.usbserial-XXXX  # specify port manually
```

Compiles production firmware (no simulation flags) and flashes it in one step.
Then open any serial monitor at **115200 baud** to watch the OBD2 output.

### Typical workflow

| Changed                    | Run                                  |
| -------------------------- | ------------------------------------ |
| Threshold constant         | `make test-unit` → `make scenario`   |
| Driving scenario data      | `make test-unit` → `make scenario`   |
| Display / UI code          | `make wokwi-build` + Wokwi simulator |
| OBD2 / BLE code            | `make deploy` → Serial Monitor       |
| Everything before car test | All four layers in order             |

---

## Project plan

Full technical details, wiring table, state machine, all references, and
the complete code skeleton are in
[esp32_obd2_dashboard_plan.md](esp32_obd2_dashboard_plan.md).

---

## Related projects

- [arduino-laser-target](https://github.com/marcelovani/arduino-laser-target)
  — uses the same SSD1306 OLED + KY-040 encoder + DFPlayer Mini pattern

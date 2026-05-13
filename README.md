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

| Component               | Notes                                                |
| ----------------------- | ---------------------------------------------------- |
| ELEGOO ESP-WROOM-32     | Main microcontroller — Bluetooth Classic             |
| ELM327 Bluetooth dongle | Plugs into car OBD2 port                             |
| 0.96" SSD1306 OLED      | 128×64 I2C display (model ep0096dtan001a)            |
| DFPlayer Mini           | MP3 playback module — drives the speaker             |
| KY-040 rotary encoder   | Navigation: rotate = cycle views, click = disconnect |
| microSD card (FAT32)    | Stores Turbo sound as `/mp3/0001.mp3`                |
| Small speaker (4–8 Ω)   | Plays the Turbo sound                                |

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

- **Sketch:** `sketches/turbo/turbo.ino` — single source, compiles for both
  real device and Wokwi simulation (`#ifdef SIMULATION`)
- **Arduino IDE 2.x**, board: ESP32 Dev Module
- Libraries: `BluetoothSerial` (built-in), `U8g2`, `DFRobotDFPlayerMini`, `Bounce2`

---

## Wokwi simulation

A browser-based circuit simulation lives in [`Emulators/Wokwi/`](Emulators/Wokwi/).
It replays a built-in driving scenario that fires two Turbo triggers, shows live
gauges on the simulated OLED, and lets the encoder cycle views — no hardware needed.

Simulated hardware (replaces real-device peripherals under `#ifdef SIMULATION`):

| Component | Pin | Role |
| --------- | --- | ---- |
| SSD1306 OLED | I2C (GPIO21/22) | Same as real device |
| KY-040 encoder | GPIO25/26/27 | Same as real device |
| Passive buzzer | GPIO17 (TX2) | Plays 900 Hz beep for 350 ms when Turbo fires (replaces DFPlayer MP3) |
| Red LED | GPIO4 | Blinks for 1 s after each Turbo fire (visual indicator) |

**Option A — wokwi.com (browser, no compilation needed)**

1. Go to [wokwi.com](https://wokwi.com) and create a new ESP32 project
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

**Steps:**

1. Connect ESP32 via USB
2. Arduino IDE → Tools → Board → **ESP32 Dev Module**
3. Tools → Port → `/dev/cu.usbserial-...` (Mac) or `COM...` (Windows)
4. Upload sketch, then open Serial Monitor (Ctrl+Shift+M) at **115200 baud**

### Typical workflow

| Changed                    | Run                                  |
| -------------------------- | ------------------------------------ |
| Threshold constant         | `make test-unit` → `make scenario`   |
| Driving scenario data      | `make test-unit` → `make scenario`   |
| Display / UI code          | `make wokwi-build` + Wokwi simulator |
| OBD2 / Bluetooth code      | Flash to ESP32 → Serial Monitor      |
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

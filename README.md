# Arduino OBD2 Turbo Blow-Off Valve Emulator

An ESP32 device that reads live OBD2 data from your car and plays a turbo
blow-off valve (Turbo) "pssssh" sound through a speaker whenever you lift off
the throttle during a gear change — emulating the iconic Fast & Furious
turbo sound.

The device also shows live gauges (throttle, RPM, speed, G-force) on a
small OLED screen.

---

## How it works

A turbo blow-off valve vents pressurised intake air when the driver lifts
off the throttle while the turbo is still spinning. This creates the
distinctive "pssssh" sound. This device detects that moment from OBD2 data:

1. Connects to the car's OBD2 port via a Bluetooth ELM327 dongle
2. Monitors throttle position and RPM at 10 Hz
3. When throttle drops rapidly from high to low while RPM is in the boost
   range (and in 1st or 2nd gear), it triggers a pre-recorded Turbo sound
   through a small speaker
4. Displays live gauges on a 0.96" OLED screen

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
| microSD card (FAT32)    | Stores Turbo sound as `/mp3/0001.mp3`                  |
| Small speaker (4–8 Ω)   | Plays the Turbo sound                                  |

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

- **Arduino IDE 2.x**, board: ESP32 Dev Module
- Libraries: `BluetoothSerial` (built-in), `U8g2`, `DFRobotDFPlayerMini`,
  `Bounce2`, `Adafruit MPU6050`, `Adafruit Unified Sensor`

---

## Build phases

| Phase | Goal                                                 |
| ----- | ---------------------------------------------------- |
| 1     | Hardware verification — OLED, IMU, encoder, DFPlayer |
| 2     | OBD2 connection — live throttle/speed/RPM in serial  |
| 3     | Turbo trigger — sound plays on gear change             |
| 4     | Full UI — OLED gauges, auto-connect, encoder nav     |
| 5     | Gear calibration for CLA180, refine thresholds       |
| 6     | Polish — NVS persistence, multiple sounds, enclosure |

---

## Wokwi simulation

A browser-based circuit simulation lives in [`Emulators/Wokwi/`](Emulators/Wokwi/).
It replays a built-in 9-second driving scenario that fires two Turbo triggers,
shows live gauges on the simulated OLED, reads G-force from the MPU6050, and
lets the encoder cycle views — no hardware needed.

**Option A — wokwi.com (browser, no compilation needed)**

1. Go to [wokwi.com](https://wokwi.com) and create a new ESP32 project
2. Copy `diagram.json`, `libraries.txt`, and `Wokwi.ino` from `Emulators/Wokwi/`
   into the project (rename `Wokwi.ino` → `sketch.ino` in the wokwi.com editor)
3. Press **Play**

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

## Project plan

Full technical details, wiring table, state machine, all references, and
the complete code skeleton are in
[esp32_obd2_dashboard_plan.md](esp32_obd2_dashboard_plan.md).

---

## Related projects

- [arduino-laser-target](https://github.com/marcelovani/arduino-laser-target)
  — uses the same SSD1306 OLED + KY-040 encoder + DFPlayer Mini pattern

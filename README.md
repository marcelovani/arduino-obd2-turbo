# Arduino OBD2 Turbo Blow-Off Valve Emulator

An ESP32 device that reads live OBD2 data from your car and plays a turbo
blow-off valve (BOV) "pssssh" sound through a speaker whenever you lift off
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
   range (and in 1st or 2nd gear), it triggers a pre-recorded BOV sound
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
| MPU6050 IMU             | Accelerometer for G-force display                    |
| KY-040 rotary encoder   | Navigation: rotate = cycle views, click = disconnect |
| microSD card (FAT32)    | Stores BOV sound as `/mp3/0001.mp3`                  |
| Small speaker (4–8 Ω)   | Plays the BOV sound                                  |

---

## BOV trigger logic

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
| 3     | BOV trigger — sound plays on gear change             |
| 4     | Full UI — OLED gauges, auto-connect, encoder nav     |
| 5     | Gear calibration for CLA180, refine thresholds       |
| 6     | Polish — NVS persistence, multiple sounds, enclosure |

---

## Project plan

Full technical details, wiring table, state machine, all references, and
the complete code skeleton are in
[esp32_obd2_dashboard_plan.md](esp32_obd2_dashboard_plan.md).

---

## Related projects

- [arduino-laser-target](https://github.com/marcelovani/arduino-laser-target)
  — uses the same SSD1306 OLED + KY-040 encoder + DFPlayer Mini pattern

# sketches

Arduino sketches for the Turbo device. The main sketch is [`turbo/turbo.ino`](turbo/turbo.ino).

## Sketch structure

The sketch is split into one `.h` file per responsibility, all included into the
slim entry point `turbo.ino`:

| File              | Responsibility                                           |
| ----------------- | -------------------------------------------------------- |
| `Config.h`        | All `#define` constants — pins, audio tracks, thresholds |
| `Settings.h`      | Runtime cfg\* variables, NVS load/save/reset             |
| `GearEstimator.h` | `estimateGear(rpm, speed)`                               |
| `Audio.h`         | DFPlayer object + `dfplayerVoice()`                      |
| `Display.h`       | U8g2 object, `showMessage()`, `drawDisplay()`            |
| `Menu.h`          | Menu state machine — state, render, execute              |
| `Encoder.h`       | ISR, button debounce, `readEncoder()`                    |
| `TurboTrigger.h`  | `checkTurbo()` — all 5 trigger conditions                |
| `Scenario.h`      | Demo drive cycle data + linear-interpolation playback    |
| `SimLoop.h`       | `doSimLoop()` — simulation phase state machine           |
| `OBD2.h`          | BLE transport, ELM327 init, live OBD2 polling            |
| `Recorder.h`      | LittleFS CSV recorder (real + DEMO builds)               |
| `WifiExport.h`    | WiFi AP + HTTP file server (real + DEMO builds)          |

## Build modes

Three build targets selected by compile-time flags:

| Flag           | Command            | Hardware                                   | OBD2                 | Audio              | Record/Export |
| -------------- | ------------------ | ------------------------------------------ | -------------------- | ------------------ | ------------- |
| `-DSIMULATION` | `make wokwi-build` | Wokwi (I2C OLED, buzzer GPIO17, LED GPIO4) | No                   | 900 Hz buzzer beep | No            |
| `-DDEMO`       | `make demo-upload` | Real ESP32 (SPI OLED, DFPlayer, speaker)   | No                   | MP3 via DFPlayer   | Yes           |
| _(none)_       | `make deploy`      | Real ESP32                                 | Yes — BLE OBD dongle | MP3 via DFPlayer   | Yes           |

The built-in scenario fires two Turbo triggers per loop: one during a 1st→2nd gear
change at ~9.5 s and one during a 2nd→3rd gear change at ~12.5 s.

> **Partition scheme:** production and DEMO builds use the `huge_app` partition
> (3 MB app + 896 KB LittleFS). BLE + WiFi libraries together exceed the default
> 1.25 MB app partition, so `make deploy` and `make demo-upload` pass
> `--build-property "build.partitions=huge_app"` automatically.

## Arduino IDE setup

Download **[Arduino IDE 2.x](https://www.arduino.cc/en/software)** and install
the ESP32 board package:

1. Open Arduino IDE → **File → Preferences**
2. Add to _Additional boards manager URLs_:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. **Tools → Board → Boards Manager** → search `esp32` → install **esp32 by Espressif**
4. Select board: **Tools → Board → ESP32 Dev Module**

## arduino-cli (command-line builds)

Required for `make wokwi-build`, `make deploy`, and `make demo-upload`.

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

## Arduino libraries

| Library               | Install via                                            |
| --------------------- | ------------------------------------------------------ |
| `U8g2`                | Arduino IDE Library Manager or `make wokwi-setup`      |
| `DFRobotDFPlayerMini` | Arduino IDE Library Manager                            |
| `Bounce2`             | Arduino IDE Library Manager or `make wokwi-setup`      |
| `ESP32 BLE Arduino`   | Built-in (part of ESP32 board package — `BLEDevice.h`) |

## Flashing to real hardware

```bash
make deploy                              # auto-detect USB port
make deploy PORT=/dev/cu.usbserial-XXXX  # specify port manually

make demo-upload                         # DEMO mode (no OBD2 needed)
```

Open a serial monitor at **115200 baud** after flashing to watch live OBD2 output:

```
[OBD] TPS=87.0 RPM=3400 Speed=60
[OBD] TPS=4.0  RPM=3300 Speed=60
[Turbo] gear=1 prev_tps=87.0 tps=4.0 rpm=3300
```

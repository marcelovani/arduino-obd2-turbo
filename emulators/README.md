# emulators

Browser-based and desktop circuit simulation of the Turbo device — no hardware required.

## wokwi

The [`wokwi/`](wokwi/) folder contains a full circuit simulation that replays a built-in
driving scenario, fires two Turbo triggers, and shows live gauges on a virtual OLED.

### Simulated hardware

Under `#ifdef SIMULATION`, real-device peripherals are replaced with:

| Component      | Pin             | Role                                                                  |
| -------------- | --------------- | --------------------------------------------------------------------- |
| SSD1306 OLED   | I2C (GPIO21/22) | Same display, but I2C — Wokwi's SSD1306 component only supports I2C   |
| KY-040 encoder | GPIO25/26/27    | Same as real device                                                   |
| Passive buzzer | GPIO17 (TX2)    | Plays 900 Hz beep for 350 ms when Turbo fires (replaces DFPlayer MP3) |
| Red LED        | GPIO4           | Blinks for 1 s after each Turbo fire (visual indicator)               |

### Option A — wokwi.com (browser, no compilation needed)

1. Go to [wokwi.com](https://wokwi.com) and create a new ESP32 project
2. Copy `wokwi/diagram.json`, `wokwi/libraries.txt`, and `sketches/turbo/turbo.ino`
   into the project (rename the sketch to `sketch.ino`)
3. Add `-DSIMULATION` to the compile flags
4. Press **Play**

### Option B — VS Code / Windsurf extension (recommended)

See the full guide at [docs.wokwi.com/vscode/getting-started](https://docs.wokwi.com/vscode/getting-started).
The extension is recommended via `.vscode/extensions.json`.

1. Install the **Wokwi for VS Code** extension from the marketplace
2. Press **F1 → Wokwi: Request a new License**, click **GET YOUR LICENSE** on the
   Wokwi website, and approve the browser prompt — a free account is enough
3. Compile the sketch (firmware must exist before the simulator can run):

```bash
brew install arduino-cli   # Mac — one time
make wokwi-setup           # installs ESP32 board + libraries — one time
make wokwi-build           # compiles sketch → wokwi/build/
```

4. Open `wokwi/diagram.json` in VS Code and press **F1 → Wokwi: Start Simulator**

### After any sketch change

Re-run `make wokwi-build` before restarting the simulator — the extension runs
whatever firmware binary is in `wokwi/build/`. Stale firmware means pin changes,
threshold changes, and display changes will not be visible.

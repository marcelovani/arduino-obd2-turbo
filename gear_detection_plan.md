# Gear Detection Plan — Mercedes CLA180 (Manual Gearbox)

Research and implementation notes for detecting gear changes via OBD2 on a
2011 Mercedes CLA180, gasoline, manual gearbox, using a generic BLE ELM327
dongle.

---

## 1. Current implementation status (as of May 2026)

The device is fully functional. Hardware is installed in the car. Sketch is
split into 13 modules:

| File | Role |
|------|------|
| `turbo.ino` | Entry point, `loop()`, include order |
| `Config.h` | Pin assignments, compile-time thresholds |
| `Settings.h` | Runtime-adjustable `cfg*` variables, NVS persistence |
| `OBD2.h` | BLE scanning → connecting → ELM327 init → live polling |
| `GearEstimator.h` | Speed-band gear estimation |
| `TurboTrigger.h` | `checkTurbo()` trigger logic |
| `Display.h` | `drawDisplay()`, `drawParked()`, bar graphs |
| `Menu.h` | Menu state machine: Main / Settings / Edit / Recording / Export |
| `Encoder.h` | Rotary encoder read, button debounce |
| `Recorder.h` | OBD2 data recorder to LittleFS (CSV) |
| `WifiExport.h` | WiFi AP + HTTP server for log download/delete |
| `Scenario.h` | Built-in demo drive cycle |
| `SimLoop.h` | Wokwi simulation loop |

**Build modes:**

| Flag | What runs |
|------|-----------|
| (none) | Production: BLE OBD2, DFPlayer, LittleFS, WiFi export |
| `-DDEMO` | Demo drive cycle, DFPlayer, LittleFS, WiFi export |
| `-DSIMULATION` | Wokwi sim: I2C OLED, buzzer, LED — no BLE/FS/WiFi |

**Known issues resolved:**

| Issue | Root cause | Fix |
|-------|-----------|-----|
| TPS always ~10% at idle | PID `0x11` returns raw sensor angle | Switched to PID `0x45` (Relative Throttle Position, 0% at closed throttle) |
| Gear display "sticky" after gear change | Ratio `RPM ÷ speed` breaks when clutch is pressed | Replaced with speed-band estimation |

---

## 2. The gear stickiness problem

On a manual gearbox the driver presses the clutch, which momentarily decouples
engine RPM from wheel speed. During this ~200–400 ms window the ratio
`RPM ÷ speed` becomes meaningless — RPM drops to idle while speed is still
high — causing the old estimator to report a false high gear. The displayed
gear would then remain at, say, 3rd even as the car slowed to a stop.

---

## 3. Current solution — speed-band estimation

**File:** [sketches/turbo/GearEstimator.h](sketches/turbo/GearEstimator.h)

```cpp
int estimateGear(float rpm, float speed) {
  if (speed < 3.0f || rpm < 200.0f) return 0;
  if (speed < cfgSpeed12) return 1;   // default: < 50 km/h (30 mph)
  if (speed < cfgSpeed23) return 2;   // default: < 65 km/h (40 mph)
  if (speed < 145.0f)     return 3;
  if (speed < 165.0f)     return 4;
  if (speed < 200.0f)     return 5;
  return 6;
}
```

Gear is a pure function of speed. As speed drops, gear drops — no stickiness.
`cfgSpeed12` and `cfgSpeed23` are tunable in the Settings menu (0–100 km/h
and 0–150 km/h, step 5).

**CLA180 real shift points:** 1→2 at ~30 mph (48 km/h), 2→3 at ~40 mph (64 km/h).

**Limitation:** speed bands cannot distinguish gear within a band. At 60 km/h
the car could be in 2nd or 3rd depending on driver style. For the Turbo
trigger this is acceptable — the trigger gates on gear ≤ 2, and 2nd-gear
shifts happen below 65 km/h by design.

---

## 4. Can OBD2 give us the actual gear or clutch state?

Research conducted May 2026.

### Standard OBD2 PIDs (SAE J1979 / ISO 15031-5)

| PID | Name | Verdict |
|-----|------|---------|
| `0xA3` | Current Gear | Only valid for **automatic TCUs** — the ECU reports the gear the TCU selected. Manual gearboxes have no TCU that tracks gear position. Returns NODATA on CLA180. |
| `0x11` | Absolute Throttle Position | Raw sensor angle; ~10% at idle on CLA180. **Not useful.** |
| `0x45` | Relative Throttle Position | Normalised to 0% at closed throttle. **Currently in use.** |
| — | Clutch pedal position | **No standard OBD2 PID exists for clutch position** in SAE J1979. |

### Clutch Pedal Position (CPP) switch

The ECU knows whether the clutch is fully pressed or released — it uses this
for the starter interlock and cruise control cut-off. OBD fault codes
**P0830** (Switch A Circuit) and **P0833** (Switch B Circuit) reference it.
However:

- These are **diagnostic trouble codes**, not live-data PIDs.
- The CPP signal is binary (pressed / not pressed), not a continuous position.
- Mercedes-Benz exposes clutch state on its **internal CAN bus** but the exact
  proprietary PID is not documented for the W176 platform and is **not
  accessible via a generic ELM327 dongle**.
- Accessing it would require the official **XENTRY/DAS** tool or reverse-
  engineering the W176 CAN database.

### Engine Load as an indirect clutch indicator (PID `0x04`)

When the clutch is depressed mid-pull, the engine suddenly loses load. Engine
Load (calculated from MAP sensor + RPM) drops to near-zero even though RPM
is still high and the throttle is still open. This produces a distinctive
pattern:

```
TPS:    ████████░░░░░  (throttle still partly open)
RPM:    ████████▄▄░░░  (starts to drop)
Load:   ████████░░░░░  (drops to near-zero — clutch pressed)
Speed:  ████████████░  (barely changes in 200 ms)
```

This is distinct from a simple throttle lift (where TPS also drops to zero).
However:
- Requires polling one extra PID (`0104`) every 100 ms, adding ~50 ms latency per cycle.
- The Load signal is noisy and ECU-specific; false positives are likely on hills
  or during engine braking.
- For the Turbo trigger use-case this adds no value: lift-off is already
  detected via TPS drop, and we only need the gear the car was in at that
  moment, not whether the clutch was pressed.

### Conclusion

For a 2011 Mercedes CLA180 with a manual gearbox and a generic BLE ELM327
dongle, **there is no reliable OBD2 signal for clutch position or gear number**.
The speed-band approach is the best available method without manufacturer-
specific tooling.

---

## 5. Possible future improvements

### 5a. Refine speed bands from recorded data (low effort)

Use the CSV recorder (Menu → Record) to capture a full drive. Plot speed
against subjective gear in a spreadsheet or Python script to identify the
actual speed ranges for each gear on the CLA180, then update `cfgSpeed12`
and `cfgSpeed23` accordingly.

**How to record:**
1. Menu → Record → drive normally
2. Click encoder to stop → file saved to LittleFS
3. Menu → Export → connect to `TurboESP32` Wi-Fi → download CSV
4. CSV format: `ms, tps, rpm, speed`

### 5b. Engine Load delta heuristic (medium effort)

Add PID `0104` (Calculated Engine Load, 0–100%) to the 100 ms poll loop.
Detect a clutch press when Load drops sharply (e.g. >30% in one cycle) while
TPS is still above ~20%. This would allow the Turbo sound in 3rd gear too,
since we'd be detecting the actual lift event rather than relying on gear
position.

**Risk:** false positives from hills, engine braking, and MAP sensor noise.
Would need careful tuning and hysteresis.

### 5c. CAN bus sniffer — hardware approach (high effort)

A separate **MCP2515 module** wired directly to the OBD2 port CAN lines
(pin 6 = CAN-H, pin 14 = CAN-L) can read all internal CAN frames, not just
the SAE J1979 PIDs that ELM327 exposes. The W176 clutch switch CAN frame
address could be found by:

1. Recording raw CAN traffic with clutch pressed, then released, repeatedly.
2. Diffing the captures to identify which frame ID changes on each press.
3. Decoding that frame's byte/bit to get a binary clutch state.

This is the only way to get true clutch state without XENTRY. It requires
soldering a second MCU or using a second ESP32 as a CAN sniffer. High effort,
but would give reliable, sub-10 ms clutch detection.

---

## 6. Research links

- [Feasibility of clutch position over CAN/OBD2 — FT86CLUB forum](https://www.ft86club.com/forums/showthread.php?t=141330)
- [OBD2 PID for current transmission gear — Porsche 718 Forum](https://www.718forum.com/threads/obd-pid-for-current-transmission-gear.29541/)
- [OBD2 PID for current gear w164 — MBWorld.org](https://mbworld.org/forums/m-class-w164/721087-obd2-pid-current-gear-w164.html)
- [OBD-II PIDs reference — Wikipedia](https://en.wikipedia.org/wiki/OBD-II_PIDs)
- [Standard OBD2 PIDs table — CSS Electronics](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979)
- [ResearchGate: PIDs for gear, brake pedal, fuel consumption](https://www.researchgate.net/post/What_are_Parameter_IDs_in_OBD-II_for_current_gear_breaking_pedal_position_and_current_fuel_consumption)
- [Read gear number from CAN — GitHub issue](https://github.com/mkovero/7226ctrl/issues/15)
- [P0830 Clutch Pedal Switch A Circuit — RepairPal](https://repairpal.com/obd-ii-code-p0830-clutch-pedal-switch-a-circuit)
- [P0833 Clutch Pedal Switch B Circuit — TroubleCodes.net](https://www.troublecodes.net/pcodes/p0833/)

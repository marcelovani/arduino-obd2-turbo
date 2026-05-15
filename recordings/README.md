# recordings

CSV files recorded from live OBD2 data while driving. This folder is excluded from
git (listed in `.gitignore`) — files are generated on the device and exported via WiFi.

## How to record

1. Connect to OBD2 as normal (wait for "OBD2 connected!" screen)
2. Click the encoder to open the menu → select **Record**
3. The screen switches to a full-screen recording view:
   - `* REC` blinks every 500 ms (confirms recording is live)
   - Sample count increments every 100 ms
   - Current RPM and TPS shown in real time
4. Drive normally — every 100 ms a CSV row is written to flash
5. Click the encoder to **Stop & Save** — the file is flushed and you return to the menu

Each file is named `log_001.csv`, `log_002.csv`, … The counter persists across
power cycles. The ~896 KB LittleFS partition holds roughly **37 minutes** of driving data.

## How to export via WiFi

After recording, park and export without any cables:

1. Open the menu → select **Export**
2. The device creates a WiFi access point:

   | Setting  | Value                |
   | -------- | -------------------- |
   | SSID     | `TurboESP32`         |
   | Password | `turbo1234`          |
   | URL      | `http://192.168.4.1` |

3. Connect your phone or laptop to `TurboESP32`
4. Open `http://192.168.4.1` in a browser — you'll see a list of all log files
5. Click a file name to **download** it as a CSV
6. Click **Delete** to remove a file from the device
7. Click the encoder to stop the WiFi server and return to the menu

> **Note:** OBD2 polling is paused while the WiFi server is active to avoid
> radio conflicts between BLE and WiFi on the shared ESP32 antenna.

## CSV format

The first line is a settings header written at record start:

```
# throttle_high=60.0,throttle_low=10.0,rpm_min=1500.0,min_gear=1,max_gear=2,cooldown_ms=2000,speed12=50.0,speed23=65.0
```

Each subsequent row is one OBD2 poll (100 ms cadence during driving):

```csv
ms,tps,rpm,speed
8203,85.2,1520.0,2.0
8303,89.4,1680.0,4.0
```

| Column  | Unit | Notes                                   |
| ------- | ---- | --------------------------------------- |
| `ms`    | ms   | `millis()` timestamp — relative to boot |
| `tps`   | %    | Throttle position (0–100)               |
| `rpm`   | RPM  | Engine RPM                              |
| `speed` | km/h | Vehicle speed (SAE J1979 PID 0x0D)      |

## Viewing recordings

Use the recording viewer to inspect any CSV as an interactive chart:

```bash
make viewer   # then open http://localhost:8080
```

See [`viewer/README.md`](../viewer/README.md) for full details.

## Replaying as a test scenario

A recorded CSV can be fed into the Python test suite to check trigger timing:

```bash
.venv/bin/python tests/visual_monitor.py --csv recordings/log_001.csv
.venv/bin/python tests/visual_monitor.py --csv recordings/log_001.csv \
    --throttle-high 35 --throttle-low 8 --rpm-min 1200
```

To convert a recording into a permanent built-in demo scenario, paste the output
of this script into the `SCENARIO[]` array in `sketches/turbo/Scenario.h`:

```python
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
t0 = int(rows[0]['ms'])
for r in rows:
    print(f"  {{ {int(r['ms'])-t0:6d}, {float(r['tps']):5.1f}, "
          f"{float(r['rpm']):7.1f}, {float(r['speed']):5.1f} }},")
```

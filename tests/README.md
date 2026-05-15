# tests

Four testing layers, from fastest to most complete. Run them in order when you
change trigger logic, thresholds, or the driving scenario.

## Setup

Python 3.x is required. Run once to create the virtual environment and install
all dependencies:

```bash
make build
```

## 1. Unit tests — no hardware needed

Verifies all trigger logic (`parse_pid`, `estimate_gear`, `TurboTrigger`) and
replays every driving scenario to check the expected trigger count.

```bash
make test-unit
```

All tests run in under a second. Run this whenever you change a threshold in
`Config.h` or the trigger algorithm in `TurboTrigger.h` / `GearEstimator.h`.

### Files

| File / folder           | Contents                                              |
| ----------------------- | ----------------------------------------------------- |
| `unit/test_obd_logic.py`   | All unit tests — parse_pid, estimate_gear, TurboTrigger, MenuController, scenarios |
| `obd_logic.py`          | Thin re-export wrapper + `MenuController` (test-only) |
| `scenarios/definitions.py` | Driving scenario data used by scenario tests       |
| `visual_monitor.py`     | Visual scenario replay tool (see below)               |

Shared logic (`parse_pid`, `estimate_gear`, `TurboTrigger`) lives in
[`lib/turbo_logic.py`](../lib/README.md) — imported by both tests and the viewer.

## 2. Visual scenario monitor — no hardware needed

Replays driving scenarios in the terminal in real time, printing each data point
and flagging when Turbo fires. Good for checking trigger timing before touching hardware.

```bash
make scenario                                    # all scenarios
make scenario SCENARIO=first_gear_change         # one scenario
make scenario THROTTLE_HIGH=35 RPM_MIN=1200      # tune thresholds
make scenarios                                   # list available scenarios
```

Replay a real recording:

```bash
.venv/bin/python tests/visual_monitor.py --csv recordings/log_001.csv
.venv/bin/python tests/visual_monitor.py --csv recordings/log_001.csv \
    --throttle-high 35 --rpm-min 1200
```

## 3. Integration tests — mock ELM327 server

Tests the full OBD2 polling loop against a mock ELM327 server (no dongle needed).

```bash
make test-emulator
```

### Files

| File                      | Contents                              |
| ------------------------- | ------------------------------------- |
| `integration/mock_elm327.py`  | In-process mock ELM327 TCP server |
| `integration/elm_client.py`   | OBD2 client used by tests         |
| `integration/test_emulator.py`| AT command and PID polling tests  |

## 4. Run everything

```bash
make test   # unit + integration
```

## Typical workflow

| Changed                    | Run                                  |
| -------------------------- | ------------------------------------ |
| Threshold constant         | `make test-unit` → `make scenario`   |
| Driving scenario data      | `make test-unit` → `make scenario`   |
| Display / UI code          | `make wokwi-build` + Wokwi simulator |
| OBD2 / BLE code            | `make deploy` → Serial Monitor       |
| Everything before car test | All four layers in order             |

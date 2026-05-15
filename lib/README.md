# lib

Shared Python logic used by both the test suite (`tests/`) and the recording viewer (`viewer/`).

## turbo_logic.py

A Python mirror of the trigger-relevant C++ files in `sketches/turbo/`. Keeping the logic
in one place means tests and viewer always agree on when a Turbo event should fire.

| C++ source                    | Python equivalent            |
| ----------------------------- | ---------------------------- |
| `sketches/turbo/Config.h`     | Constants — auto-parsed at import time |
| `sketches/turbo/GearEstimator.h` | `estimate_gear(rpm, speed_kmh)` |
| `sketches/turbo/TurboTrigger.h`  | `TurboTrigger` class        |

### What it provides

- **Constants** (`TURBO_THROTTLE_HIGH`, `TURBO_RPM_MIN`, etc.) — read directly from
  `Config.h` with a regex at import time, so they never go out of sync.
- **`parse_pid(resp, byte_count, multiplier)`** — mirror of `OBD2.h parsePID()`.
- **`estimate_gear(rpm, speed_kmh, s12, s23)`** — speed-band gear estimation.
- **`TurboTrigger`** — stateful trigger class; call `update(tps, rpm, speed, now_ms)`
  each poll cycle; returns `True` when a spray should fire.
- **`load_config_h_as_settings()`** — returns Config.h constants as a viewer-compatible dict.

### Keeping it in sync

This file is a **manual** mirror. If you change either C++ file below, update the
corresponding function here and run `make test` to verify they agree:

| C++ file                              | Python function / class      |
| ------------------------------------- | ---------------------------- |
| `sketches/turbo/GearEstimator.h`      | `estimate_gear()`            |
| `sketches/turbo/TurboTrigger.h`       | `TurboTrigger.update()`      |

Constants in `Config.h` are parsed automatically — no manual sync needed there.

### Future improvement

Compile the C++ logic as a shared library and call it from Python via `ctypes` or
`pybind11`. That would eliminate drift entirely — tests and viewer would run the actual
C++ functions. Requires stripping Arduino-specific globals from the `.h` files first.

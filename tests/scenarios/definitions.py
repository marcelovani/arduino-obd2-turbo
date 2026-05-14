"""
Driving scenarios for Turbo trigger simulation.

Each scenario is a list of data points:
    (time_ms, tps_percent, rpm, speed_kmh)

Used by:
  - tests/visual_monitor.py   (interactive terminal display)
  - tests/unit/test_obd_logic.py  (assert expected trigger counts)
"""

# ── Scenario type annotation ──────────────────────────────────────────────
# list of (time_ms, tps, rpm, speed_kmh)
DataPoint = tuple[int, float, float, float]
Scenario  = list[DataPoint]


SCENARIOS: dict[str, Scenario] = {

    # ── Idle — no Turbo expected ────────────────────────────────────────────
    "idle": [
        (0,    0,  800, 0),
        (500,  0,  800, 0),
        (1000, 0,  850, 0),
        (1500, 0,  800, 0),
    ],

    # ── 2nd gear pull, lift to shift to 3rd — two Turbo triggers ─────────
    # Speed bands: <50 km/h=1st  <65 km/h=2nd  ≥65 km/h=3rd+
    # Turbo #1 at ~1500ms (2nd gear, speed 62 km/h, RPM 3500 > 3000, TPS 85→4).
    # Turbo #2 at ~3600ms (2nd gear, 2100ms after #1 — cooldown expired).
    # No trigger at 4300ms: speed 95 km/h → gear 3 > max_gear 2.
    "first_gear_change": [
        (0,     0,   800,   0),   # idle
        (300,  30,  1600,  30),   # 1st gear (speed 30 < 50 km/h)
        (600,  65,  2800,  52),   # hard pull in 2nd (speed 52 → gear 2)
        (900,  80,  3200,  58),   # hard pull in 2nd (speed 58 → gear 2)
        (1200, 85,  3600,  60),   # near shift point (speed 60 → gear 2)
        (1500,  4,  3500,  62),   # *** Turbo #1 *** (speed 62 < 65 → gear 2, TPS 85→4)
        (1600, 65,  2800,  52),   # back on throttle (speed 52 → gear 2)
        (1900, 70,  3100,  55),   # accelerating (speed 55 → gear 2)
        (2200, 75,  3300,  58),   # (speed 58 → gear 2)
        (2800, 80,  3500,  60),   # near peak again (speed 60 → gear 2)
        (3600,  4,  3200,  62),   # *** Turbo #2 *** (speed 62 < 65 → gear 2, 2100ms after #1)
        (3700, 40,  2200,  70),   # easing off (speed 70 ≥ 65 → gear 3)
        (4000, 60,  2500,  90),   # (speed 90 → gear 3) — shifted up
        (4300,  4,  2200,  95),   # lift off in 3rd — no Turbo (gear 3 > max_gear 2)
    ],

    # ── Hard pull in 2nd, lift to shift to 3rd — one Turbo trigger ────────
    # RPM > 3000, TPS > 60, gear 2 → trigger on sharp lift-off.
    "second_gear_hard_pull": [
        (0,    10, 1400,  25),    # 1st gear (speed 25 < 50 km/h)
        (300,  15, 1800,  32),    # 1st gear (speed 32 < 50 km/h)
        (600,  70, 2800,  52),    # 2nd gear (speed 52 → gear 2)
        (900,  85, 3400,  56),    # hard pull (speed 56 → gear 2, RPM > 3000)
        (1200, 80, 3800,  60),    # near peak (speed 60 → gear 2)
        (1500,  4, 3600,  62),    # *** Turbo *** (speed 62 < 65 → gear 2, TPS 80→4)
        (1800, 50, 2800,  70),    # back on throttle (speed 70 ≥ 65 → gear 3)
    ],

    # ── Should NOT trigger: RPM too low ──────────────────────────────────
    # Throttle drops sharply in 1st gear but RPM is ≤ TURBO_RPM_MIN (3000).
    "no_turbo_low_rpm": [
        (0,    0,   800,   0),
        (400,  65,  1500,  28),   # TPS high but RPM not > 3000 (speed 28 → gear 1)
        (800,   4,  1400,  30),   # lift off — no Turbo (RPM 1400 ≤ 3000)
        (1200,  0,  1200,  28),
    ],

    # ── Should NOT trigger: gradual throttle release ──────────────────────
    # In 2nd gear near peak RPM but throttle drops gradually — TPS never
    # exceeds TURBO_THROTTLE_HIGH (60%) before dropping below 10%.
    "no_turbo_gradual_release": [
        (0,    0,   800,   0),
        (400,  30,  3200,  58),   # TPS moderate, 2nd gear (speed 58 → gear 2)
        (500,  25,  3100,  60),   # gradual drop — prev_TPS 30 not > 60, no trigger
        (600,  18,  3000,  62),   # prev_TPS 25 not > 60 — no trigger
        (700,  12,  2900,  64),   # prev_TPS 18 not > 60 — no trigger
        (800,   8,  2800,  64),   # prev_TPS 12 not > 60 — no trigger (speed kept < 65)
    ],

    # ── Should NOT trigger: gear too high ────────────────────────────────
    # Sharp lift-off but already in 3rd gear (speed ≥ 65 km/h, > max_gear 2).
    "no_turbo_high_gear": [
        (0,    0,  1000,   0),
        (400,  70, 2500, 100),    # 3rd gear (speed 100 → gear 3)
        (800,  80, 2800, 110),    # 3rd gear (speed 110 → gear 3)
        (1200,  4, 2600, 115),    # lift off in 3rd — no Turbo (gear 3 > max_gear 2)
    ],

    # ── Cooldown respected ───────────────────────────────────────────────
    # Two lifts close together in 2nd gear — second blocked by cooldown,
    # third fires after cooldown expires.
    "cooldown_test": [
        (0,    0,   800,   0),
        (400,  75,  3200,  60),   # 2nd gear (speed 60 → gear 2), RPM > 3000
        (800,   4,  3100,  62),   # *** Turbo #1 *** (speed 62 < 65 → gear 2, TPS 75→4)
        (900,  70,  3000,  58),   # back on throttle (speed 58 → gear 2)
        (1200,  4,  3100,  60),   # within 2s cooldown — NO Turbo (speed 60 → gear 2)
        (2900, 75,  3200,  60),   # cooldown expired (speed 60 → gear 2)
        (3300,  4,  3100,  62),   # *** Turbo #2 *** (speed 62 < 65 → gear 2, 2500ms after #1)
    ],
}

# Expected Turbo counts per scenario — used in unit tests
EXPECTED_TURBO_COUNTS: dict[str, int] = {
    "idle":                    0,
    "first_gear_change":       2,
    "second_gear_hard_pull":   1,
    "no_turbo_low_rpm":          0,
    "no_turbo_gradual_release":  0,
    "no_turbo_high_gear":        0,
    "cooldown_test":           2,
}

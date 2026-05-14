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

    # ── 2nd gear pull, shift to 3rd — two Turbo triggers ──────────────────
    # Gear thresholds (ratio = RPM / km/h): >50=1st  >33=2nd  >19=3rd
    # Turbo #1 at ~1500ms (2nd gear, RPM 3400 > 3000, TPS 85→4).
    # Turbo #2 at ~3600ms (2nd gear, 2100ms after #1 — cooldown expired).
    # No trigger at 4300ms: gear 3 > max_gear 2.
    "first_gear_change": [
        (0,     0,   800,   0),   # idle
        (300,  30,  1400,  30),   # entering 2nd gear (ratio 47 → gear 1 still)
        (600,  65,  2500,  62),   # hard pull in 2nd (ratio 40 → gear 2)
        (900,  80,  3100,  75),   # near peak (ratio 41 → gear 2, RPM > 3000)
        (1200, 85,  3500,  82),   # near shift point (ratio 43 → gear 2)
        (1500,  4,  3400,  84),   # *** Turbo #1 *** (ratio 40 → gear 2, TPS 85→4, RPM 3400)
        (1600, 65,  2600,  80),   # back on throttle (ratio 33 → gear 2)
        (1900, 70,  3100,  80),   # accelerating (ratio 39 → gear 2)
        (2200, 75,  3300,  82),   # (ratio 40 → gear 2)
        (2800, 80,  3500,  85),   # near peak again (ratio 41 → gear 2)
        (3600,  4,  3200,  87),   # *** Turbo #2 *** (ratio 37 → gear 2, 2100ms after #1)
        (3700, 40,  2200,  90),   # easing off
        (4000, 60,  2500,  96),   # (ratio 26 → gear 3) — shifted up
        (4300,  4,  2200,  98),   # lift off in 3rd — no Turbo (gear 3 > max_gear 2)
    ],

    # ── Hard pull in 2nd, lift to shift to 3rd — one Turbo trigger ────────
    # RPM > 3000, TPS > 60, gear 2 → trigger on sharp lift-off.
    "second_gear_hard_pull": [
        (0,    10, 1400,  40),    # cruising (ratio 35 → gear 2)
        (300,  15, 1600,  44),    # (ratio 36 → gear 2)
        (600,  70, 2500,  60),    # floor it (ratio 42 → gear 2)
        (900,  85, 3200,  72),    # hard pull (ratio 44 → gear 2, RPM > 3000)
        (1200, 80, 3600,  82),    # near peak (ratio 44 → gear 2)
        (1500,  4, 3400,  85),    # *** Turbo *** (ratio 40 → gear 2, TPS 80→4, RPM 3400)
        (1800, 50, 2500,  88),    # back on throttle
    ],

    # ── Should NOT trigger: RPM too low ──────────────────────────────────
    # Throttle drops sharply in 2nd gear but RPM is below TURBO_RPM_MIN (3000).
    "no_turbo_low_rpm": [
        (0,    0,   800,   0),
        (400,  65,  1500,  42),   # TPS high but RPM < 3000 (ratio 36 → gear 2)
        (800,   4,  1400,  44),   # lift off — no Turbo (RPM 1400 < 3000)
        (1200,  0,  1200,  40),
    ],

    # ── Should NOT trigger: gradual throttle release ──────────────────────
    # In 2nd gear near peak RPM but throttle drops gradually — prev_TPS is
    # never above 60 at the same time current TPS is below 10.
    "no_turbo_gradual_release": [
        (0,    0,   800,   0),
        (400,  70,  3200,  80),   # TPS high (ratio 40 → gear 2, RPM > 3000)
        (500,  55,  3100,  82),   # gradual drop — prev_TPS 70 > 60, but TPS 55 not < 10
        (600,  40,  3000,  84),   # prev_TPS 55, not > 60 — no trigger
        (700,  25,  2900,  86),   # prev_TPS 40, not > 60 — no trigger
        (800,   8,  2800,  88),   # prev_TPS 25, not > 60 — no trigger
    ],

    # ── Should NOT trigger: gear too high ────────────────────────────────
    # Sharp lift-off but already in 3rd gear (ratio 19–33, > max_gear 2).
    "no_turbo_high_gear": [
        (0,    0,  1000,   0),
        (400,  70, 2500, 100),    # 3rd gear (ratio 25 → gear 3)
        (800,  80, 2800, 110),    # 3rd gear (ratio 25 → gear 3)
        (1200,  4, 2600, 115),    # lift off in 3rd — no Turbo (gear 3 > max_gear 2)
    ],

    # ── Cooldown respected ───────────────────────────────────────────────
    # Two lifts close together in 2nd gear — second blocked by cooldown,
    # third fires after cooldown expires.
    "cooldown_test": [
        (0,    0,   800,   0),
        (400,  75,  3200,  80),   # ratio 40 → gear 2, RPM > 3000
        (800,   4,  3100,  82),   # *** Turbo #1 *** (ratio 38 → gear 2, TPS 75→4)
        (900,  70,  3000,  78),   # back on throttle (ratio 38 → gear 2)
        (1200,  4,  3100,  80),   # within 2s cooldown — NO Turbo (ratio 39 → gear 2)
        (2900, 75,  3200,  80),   # cooldown expired (ratio 40 → gear 2)
        (3300,  4,  3100,  82),   # *** Turbo #2 *** (ratio 38 → gear 2, 2500ms after #1)
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

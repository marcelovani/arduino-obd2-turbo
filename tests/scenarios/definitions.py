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

    # ── First and second gear changes — two Turbo triggers ─────────────────
    # Gear thresholds (ratio = RPM / km/h): >50=1st  >33=2nd  >19=3rd
    # Turbo #1 at ~1500ms (1st gear). Turbo #2 at ~3600ms (1st gear again —
    # ratio still >50 at these speeds). No 3rd trigger: within 2s cooldown.
    "first_gear_change": [
        (0,     0,   800,  0),    # idle
        (300,  30,  1200,  8),    # pulling away
        (600,  65,  2200, 15),    # pressing hard in 1st (ratio 147 → gear 1)
        (900,  80,  3000, 22),    # hard acceleration (ratio 136 → gear 1)
        (1200, 85,  3500, 26),    # near red-line in 1st (ratio 135 → gear 1)
        (1500,  4,  3300, 28),    # *** Turbo #1 *** (ratio 118 → gear 1)
        (1600, 50,  2200, 32),    # back on throttle (ratio 69 → gear 1)
        (1900, 70,  2800, 40),    # accelerating (ratio 70 → gear 1)
        (2200, 75,  3200, 42),    # (ratio 76 → gear 1)
        (2800, 80,  3500, 44),    # (ratio 79 → gear 1)
        (3600,  4,  3300, 46),    # *** Turbo #2 *** (ratio 72 → gear 1, 2100ms after #1)
        (3700, 40,  2000, 50),    # easing off
        (4000, 60,  2800, 58),    # (ratio 48 → gear 2) — no Turbo (within cooldown)
        (4300,  4,  2600, 61),    # lift off — no Turbo (within cooldown)
    ],

    # ── Cruising then hard acceleration — one Turbo trigger ───────────────
    # ratio 69 → gear 1 at these speeds; Turbo fires on lift-off.
    "second_gear_hard_pull": [
        (0,    10, 1400, 20),     # cruising (ratio 70 → gear 1)
        (300,  15, 1500, 22),
        (600,  70, 2500, 28),     # floor it (ratio 89 → gear 1)
        (900,  85, 3200, 36),     # hard pull (ratio 89 → gear 1)
        (1200, 80, 3600, 44),     # (ratio 82 → gear 1)
        (1500,  4, 3400, 47),     # *** LIFT OFF → Turbo expected (ratio 72 → gear 1) ***
        (1800, 50, 2500, 52),
    ],

    # ── Should NOT trigger: RPM too low ──────────────────────────────────
    # Throttle drops sharply but RPM is below TURBO_RPM_MIN (1500).
    "no_turbo_low_rpm": [
        (0,    0,   800,  0),
        (400,  60,  1200, 10),    # TPS high but RPM < 1500 (ratio 120 → gear 1)
        (800,   4,  1100, 12),    # lift off — no Turbo (RPM < minimum)
        (1200,  0,   900,  8),
    ],

    # ── Should NOT trigger: gradual throttle release ──────────────────────
    # Throttle drops slowly — prev_TPS is never above threshold at the same
    # time current TPS is below the low threshold.
    "no_turbo_gradual_release": [
        (0,    0,   800,  0),
        (400,  70,  2800, 20),    # TPS high (ratio 140 → gear 1)
        (500,  55,  2700, 22),    # gradual drop
        (600,  40,  2600, 24),    # at threshold, not above
        (700,  25,  2500, 26),    # prev_TPS was 40, not > 40 — no trigger
        (800,   8,  2400, 28),    # prev_TPS was 25, not > 40 — no trigger
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
    # Two gear changes close together — second is blocked by cooldown,
    # third happens after cooldown and should fire.
    "cooldown_test": [
        (0,    0,   800,  0),
        (400,  75,  3200, 22),    # ratio 145 → gear 1
        (800,   4,  3000, 25),    # *** Turbo #1 *** (ratio 120 → gear 1)
        (900,  70,  2500, 28),    # immediately back on throttle (ratio 89 → gear 1)
        (1200,  4,  2400, 30),    # within 2s cooldown — NO Turbo (ratio 80 → gear 1)
        (2900, 75,  3100, 35),    # cooldown expired >2s (ratio 89 → gear 1)
        (3300,  4,  2900, 38),    # *** Turbo #2 *** (ratio 76 → gear 1)
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

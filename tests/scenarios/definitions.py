"""
Driving scenarios for BOV trigger simulation.

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

    # ── Idle — no BOV expected ────────────────────────────────────────────
    "idle": [
        (0,    0,  800, 0),
        (500,  0,  800, 0),
        (1000, 0,  850, 0),
        (1500, 0,  800, 0),
    ],

    # ── First and second gear changes — two BOV triggers ─────────────────
    # BOV #1 at ~1500ms (1st→2nd).
    # BOV #2 at ~3600ms (2nd→3rd) — must be >2000ms after BOV #1 for cooldown.
    # No BOV in 3rd gear (gear > max_gear).
    "first_gear_change": [
        (0,     0,   800,  0),    # idle
        (300,  30,  1200,  8),    # pulling away
        (600,  65,  2200, 15),    # pressing hard in 1st (ratio 147 → gear 1)
        (900,  80,  3000, 22),    # hard acceleration (ratio 136 → gear 1)
        (1200, 85,  3500, 26),    # near red-line in 1st (ratio 135 → gear 1)
        (1500,  4,  3300, 28),    # *** BOV #1 *** (ratio 118 → gear 2, rpm 3300 > 1500)
        (1600, 50,  2200, 32),    # into 2nd, back on throttle
        (1900, 70,  2800, 40),    # hard in 2nd (ratio 70 → gear 2)
        (2200, 75,  3200, 42),    # still in 2nd (ratio 76 → gear 2)
        (2800, 80,  3500, 44),    # near top of 2nd (ratio 79 → gear 2)
        (3600,  4,  3300, 46),    # *** BOV #2 *** (2100ms after BOV#1, ratio 72 → gear 2)
        (3700, 40,  2000, 50),    # into 3rd
        (4000, 60,  2800, 58),    # 3rd gear (ratio 48 → gear 3) — no BOV
        (4300,  4,  2600, 61),    # lift in 3rd — no BOV (gear 3 > max_gear 2)
    ],

    # ── Cruising then hard acceleration from 2nd ──────────────────────────
    # Start in 2nd at low speed, floor it, lift off.
    "second_gear_hard_pull": [
        (0,    10, 1400, 20),     # cruising in 2nd, low TPS
        (300,  15, 1500, 22),
        (600,  70, 2500, 28),     # floor it
        (900,  85, 3200, 36),     # hard pull
        (1200, 80, 3600, 44),     # approaching 3rd gear territory
        (1500,  4, 3400, 47),     # *** LIFT OFF → BOV expected ***
        (1800, 50, 2500, 52),
    ],

    # ── Should NOT trigger: RPM too low ──────────────────────────────────
    # Throttle drops sharply but RPM is below BOV_RPM_MIN (1500).
    "no_bov_low_rpm": [
        (0,    0,   800,  0),
        (400,  60,  1200, 10),    # TPS high but RPM < 1500
        (800,   4,  1100, 12),    # lift off — no BOV (RPM < minimum)
        (1200,  0,   900,  8),
    ],

    # ── Should NOT trigger: gradual throttle release ──────────────────────
    # Throttle drops slowly — prev_TPS never stays above threshold long
    # enough to be followed by a snap below the low threshold.
    "no_bov_gradual_release": [
        (0,    0,   800,  0),
        (400,  70,  2800, 20),    # TPS high
        (500,  55,  2700, 22),    # gradual drop
        (600,  40,  2600, 24),    # at threshold, not above
        (700,  25,  2500, 26),    # prev_TPS was 40, not > 40 — no trigger
        (800,   8,  2400, 28),    # prev_TPS was 25, not > 40 — no trigger
    ],

    # ── Should NOT trigger: gear too high ────────────────────────────────
    # Sharp lift-off but already in 3rd gear (RPM/speed ratio < gear-2 range).
    "no_bov_high_gear": [
        (0,    0,  1000,  0),
        (400,  70, 2800,  60),    # 3rd gear territory (ratio ~47)
        (800,  80, 3200,  68),
        (1200,  4, 3000,  72),    # lift off in 3rd — no BOV
    ],

    # ── Cooldown respected ───────────────────────────────────────────────
    # Two gear changes close together — second is blocked by cooldown,
    # third happens after cooldown and should fire.
    "cooldown_test": [
        (0,    0,   800,  0),
        (400,  75,  3200, 22),
        (800,   4,  3000, 25),    # *** BOV #1 ***
        (900,  70,  2500, 28),    # immediately back on throttle
        (1200,  4,  2400, 30),    # within 2s cooldown — NO BOV
        (2900, 75,  3100, 35),    # cooldown expired (>2s since BOV #1)
        (3300,  4,  2900, 38),    # *** BOV #2 ***
    ],
}

# Expected BOV counts per scenario — used in unit tests
EXPECTED_BOV_COUNTS: dict[str, int] = {
    "idle":                    0,
    "first_gear_change":       2,
    "second_gear_hard_pull":   1,
    "no_bov_low_rpm":          0,
    "no_bov_gradual_release":  0,
    "no_bov_high_gear":        0,
    "cooldown_test":           2,
}

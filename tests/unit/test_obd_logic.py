"""
Unit tests for OBD logic — parsePID, estimateGear, TurboTrigger.

These tests run entirely in Python with no hardware and no emulator.
They verify that the logic in tests/obd_logic.py (which mirrors the C++
sketch code) behaves correctly for all edge cases.

Run with: make test-unit
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import pytest
from obd_logic import parse_pid, estimate_gear, TurboTrigger
from scenarios.definitions import SCENARIOS, EXPECTED_TURBO_COUNTS


# ── parse_pid ─────────────────────────────────────────────────────────────

class TestParsePid:

    def test_speed_60kmh(self):
        # PID 010D, speed = 0x3C = 60 km/h
        assert parse_pid("410D3C", 1, 1.0) == pytest.approx(60.0)

    def test_speed_zero(self):
        assert parse_pid("410D00", 1, 1.0) == pytest.approx(0.0)

    def test_speed_max(self):
        # 0xFF = 255 km/h
        assert parse_pid("410DFF", 1, 1.0) == pytest.approx(255.0)

    def test_rpm_3000(self):
        # PID 010C, RPM = (0x2E * 256 + 0xE0) / 4 = 3000
        assert parse_pid("410C2EE0", 2, 0.25) == pytest.approx(3000.0)

    def test_rpm_zero(self):
        assert parse_pid("410C0000", 2, 0.25) == pytest.approx(0.0)

    def test_throttle_50_percent(self):
        # 0x80 = 128, 128 * (100/255) ≈ 50.2%
        result = parse_pid("41117F", 1, 100.0 / 255.0)
        assert 49.0 < result < 51.0

    def test_throttle_full(self):
        # 0xFF = 255, 255 * (100/255) = 100%
        assert parse_pid("4111FF", 1, 100.0 / 255.0) == pytest.approx(100.0)

    def test_throttle_zero(self):
        assert parse_pid("411100", 1, 100.0 / 255.0) == pytest.approx(0.0)

    def test_empty_response_returns_minus_one(self):
        assert parse_pid("", 1, 1.0) == -1.0

    def test_too_short_returns_minus_one(self):
        assert parse_pid("410D", 1, 1.0) == -1.0   # missing data bytes

    def test_no_data_response_returns_minus_one(self):
        # "NODATA" is the ELM327 response when PID not supported
        assert parse_pid("NODATA", 1, 1.0) == -1.0

    def test_case_insensitive(self):
        assert parse_pid("410d3c", 1, 1.0) == pytest.approx(60.0)

    def test_response_with_extra_whitespace(self):
        assert parse_pid("  410D3C  ", 1, 1.0) == pytest.approx(60.0)


# ── estimate_gear ─────────────────────────────────────────────────────────

class TestEstimateGear:

    def test_stopped_returns_zero(self):
        assert estimate_gear(0, 0) == 0

    def test_engine_off_returns_zero(self):
        assert estimate_gear(0, 30) == 0

    def test_very_slow_returns_zero(self):
        assert estimate_gear(1000, 1) == 0  # speed < 2 km/h

    def test_first_gear(self):
        # Typical 1st gear: RPM 3000 at 20 km/h → ratio 150
        assert estimate_gear(3000, 20) == 1

    def test_second_gear(self):
        # 2nd gear: ratio 33–50. RPM 2600 at 65 km/h → ratio 40
        assert estimate_gear(2600, 65) == 2

    def test_third_gear(self):
        # 3rd gear: ratio 19–33. RPM 2500 at 100 km/h → ratio 25
        assert estimate_gear(2500, 100) == 3

    def test_fourth_gear(self):
        # 4th gear: ratio 12–19. RPM 2200 at 150 km/h → ratio ~14.7
        assert estimate_gear(2200, 150) == 4

    def test_fifth_gear(self):
        # 5th gear: ratio 8–12. RPM 2000 at 200 km/h → ratio 10
        assert estimate_gear(2000, 200) == 5

    def test_sixth_gear(self):
        # 6th gear: ratio < 8. RPM 1800 at 250 km/h → ratio 7.2
        assert estimate_gear(1800, 250) == 6


# ── TurboTrigger ────────────────────────────────────────────────────────────

class TestTurboTrigger:

    def _make_trigger(self, **kwargs):
        return TurboTrigger(**kwargs)

    def test_no_trigger_at_idle(self):
        turbo = TurboTrigger()
        # idle: TPS low, RPM low
        assert not turbo.update(0, 800, 0, 0)
        assert not turbo.update(0, 800, 0, 100)
        assert turbo.count == 0

    def test_triggers_on_sharp_lift_in_second_gear(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 80, 0)    # high TPS, 2nd gear (ratio 40)
        triggered = turbo.update(4, 3100, 82, 100)   # lift off
        assert triggered
        assert turbo.count == 1

    def test_triggers_in_first_gear(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 22, 0)    # 1st gear (ratio 145 → gear 1)
        triggered = turbo.update(4, 3100, 24, 100)
        assert triggered
        assert turbo.count == 1

    def test_no_trigger_when_rpm_too_low(self):
        turbo = TurboTrigger()
        turbo.update(75, 1500, 42, 0)    # high TPS, 2nd gear (ratio 36), but RPM < 3000
        triggered = turbo.update(4, 1400, 44, 100)
        assert not triggered
        assert turbo.count == 0

    def test_no_trigger_in_third_gear(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 120, 0)   # 3rd gear (ratio 27 → gear 3)
        triggered = turbo.update(4, 3100, 122, 100)
        assert not triggered
        assert turbo.count == 0

    def test_no_trigger_when_prev_tps_not_high_enough(self):
        turbo = TurboTrigger()
        turbo.update(55, 3200, 80, 0)    # TPS only 55% — below 60% threshold
        triggered = turbo.update(4, 3100, 82, 100)
        assert not triggered

    def test_no_trigger_when_curr_tps_not_low_enough(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 22, 0)
        triggered = turbo.update(15, 3100, 24, 100)  # 15% — aturboe 10% threshold
        assert not triggered

    def test_cooldown_blocks_rapid_second_trigger(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 80, 0)
        turbo.update(4,  3100, 82, 100)   # Turbo #1 at t=100ms (gear 2, ratio 38)
        turbo.update(80, 3200, 80, 200)   # back on throttle
        triggered = turbo.update(4, 3100, 82, 300)   # within 2s cooldown
        assert not triggered
        assert turbo.count == 1

    def test_cooldown_allows_trigger_after_expiry(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 80, 0)
        turbo.update(4,  3100, 82, 100)   # Turbo #1 (gear 2, ratio 38)
        turbo.update(80, 3200, 80, 200)
        turbo.update(4,  3100, 82, 300)   # blocked by cooldown
        turbo.update(80, 3200, 80, 2200)  # after cooldown
        triggered = turbo.update(4, 3100, 82, 2300)  # Turbo #2
        assert triggered
        assert turbo.count == 2

    def test_custom_thresholds(self):
        # More sensitive: lower throttle_high=25%, lower rpm_min=1000, min_gear=1
        # Speed=8 km/h → RPM/speed ratio=150 → gear 1; min_gear=1 allows it
        turbo = TurboTrigger(throttle_high=25, rpm_min=1000, min_gear=1)
        turbo.update(30, 1200, 8, 0)
        triggered = turbo.update(4, 1100, 8, 100)
        assert triggered

    def test_event_log_recorded(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 80, 0)   # 2nd gear (ratio 40)
        turbo.update(4,  3100, 82, 100)
        assert len(turbo.events) == 1
        event = turbo.events[0]
        assert event["gear"] == 2
        assert event["prev_tps"] == 80
        assert event["tps"] == 4


# ── Scenario-level tests ─────────────────────────────────────────────────
# Replay each scenario and assert the expected number of Turbo triggers.

class TestScenarios:

    @pytest.mark.parametrize("name", list(SCENARIOS.keys()))
    def test_scenario_turbo_count(self, name):
        """Replay a scenario and check the Turbo trigger count matches expectation."""
        scenario   = SCENARIOS[name]
        expected   = EXPECTED_TURBO_COUNTS[name]
        turbo        = TurboTrigger()

        for (time_ms, tps, rpm, speed) in scenario:
            turbo.update(tps, rpm, speed, time_ms)

        assert turbo.count == expected, (
            f"Scenario '{name}': expected {expected} Turbo trigger(s), "
            f"got {turbo.count}. Events: {turbo.events}"
        )

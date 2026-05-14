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
from obd_logic import parse_pid, estimate_gear, TurboTrigger, MenuController
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
        # 1st gear: speed < 50 km/h
        assert estimate_gear(3000, 20) == 1

    def test_second_gear(self):
        # 2nd gear: 50 ≤ speed < 80 km/h
        assert estimate_gear(2800, 55) == 2

    def test_third_gear(self):
        # 3rd gear: 80 ≤ speed < 145 km/h
        assert estimate_gear(2500, 100) == 3

    def test_fourth_gear(self):
        # 4th gear: 145 ≤ speed < 165 km/h
        assert estimate_gear(2200, 150) == 4

    def test_fifth_gear(self):
        # 5th gear: 165 ≤ speed < 200 km/h
        assert estimate_gear(2000, 185) == 5

    def test_sixth_gear(self):
        # 6th gear: speed ≥ 200 km/h
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
        turbo.update(80, 3200, 58, 0)    # high TPS, 2nd gear (speed 58 → gear 2)
        triggered = turbo.update(4, 3100, 58, 100)   # lift off
        assert triggered
        assert turbo.count == 1

    def test_triggers_in_first_gear(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 22, 0)    # 1st gear (speed 22 < 50 → gear 1)
        triggered = turbo.update(4, 3100, 24, 100)
        assert triggered
        assert turbo.count == 1

    def test_no_trigger_when_rpm_too_low(self):
        turbo = TurboTrigger()
        turbo.update(75, 1500, 28, 0)    # high TPS, 1st gear (speed 28 < 50), RPM not > 3000
        triggered = turbo.update(4, 1400, 30, 100)   # lift off — RPM 1400 < 3000
        assert not triggered
        assert turbo.count == 0

    def test_no_trigger_in_third_gear(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 120, 0)   # 3rd gear (speed 120 ≥ 65 → gear 3)
        triggered = turbo.update(4, 3100, 122, 100)
        assert not triggered
        assert turbo.count == 0

    def test_no_trigger_when_prev_tps_not_high_enough(self):
        turbo = TurboTrigger()
        turbo.update(55, 3200, 58, 0)    # TPS 55% — below 60% threshold, 2nd gear (speed 58)
        triggered = turbo.update(4, 3100, 58, 100)   # lift off — prev_TPS 55 < 60% → no trigger
        assert not triggered

    def test_no_trigger_when_curr_tps_not_low_enough(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 22, 0)
        triggered = turbo.update(15, 3100, 24, 100)  # 15% — aturboe 10% threshold
        assert not triggered

    def test_cooldown_blocks_rapid_second_trigger(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 58, 0)
        turbo.update(4,  3100, 58, 100)   # Turbo #1 at t=100ms (speed 58 → gear 2)
        turbo.update(80, 3200, 58, 200)   # back on throttle
        triggered = turbo.update(4, 3100, 58, 300)   # within 2s cooldown
        assert not triggered
        assert turbo.count == 1

    def test_cooldown_allows_trigger_after_expiry(self):
        turbo = TurboTrigger()
        turbo.update(80, 3200, 58, 0)
        turbo.update(4,  3100, 58, 100)   # Turbo #1 (speed 58 → gear 2)
        turbo.update(80, 3200, 58, 200)
        turbo.update(4,  3100, 58, 300)   # blocked by cooldown
        turbo.update(80, 3200, 58, 2200)  # after cooldown
        triggered = turbo.update(4, 3100, 58, 2300)  # Turbo #2
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
        turbo.update(80, 3200, 58, 0)   # 2nd gear (speed 58 → gear 2)
        turbo.update(4,  3100, 58, 100)
        assert len(turbo.events) == 1
        event = turbo.events[0]
        assert event["gear"] == 2
        assert event["prev_tps"] == 80
        assert event["tps"] == 4


# ── MenuController ────────────────────────────────────────────────────────

class TestMenuBehavior:

    # ── Initial state ──────────────────────────────────────────────────────

    def test_initial_state(self):
        menu = MenuController()
        assert menu.system_on    is True
        assert menu.demo_mode    is False
        assert menu.menu_state   == "closed"
        assert menu.main_sel     == 0
        assert menu.settings_sel == 0

    # ── Opening the menu ──────────────────────────────────────────────────

    def test_button_press_opens_main_menu(self):
        menu = MenuController()
        menu.button_press()
        assert menu.menu_state == "main"
        assert menu.main_sel   == 0

    def test_rotate_does_nothing_when_closed(self):
        menu = MenuController()
        menu.rotate(1)
        assert menu.main_sel == 0

    # ── Main menu navigation ──────────────────────────────────────────────

    def test_rotate_moves_main_selection(self):
        menu = MenuController()
        menu.button_press()
        menu.rotate(1)
        assert menu.main_sel == 1

    def test_rotate_wraps_forward(self):
        menu = MenuController()
        menu.button_press()          # main menu, sel=0
        for _ in range(4):
            menu.rotate(1)           # 1→2→3→0
        assert menu.main_sel == 0

    def test_rotate_wraps_backward(self):
        menu = MenuController()
        menu.button_press()          # sel=0
        menu.rotate(-1)              # wraps to 3 (Exit)
        assert menu.main_sel == 3

    # ── Power option ──────────────────────────────────────────────────────

    def test_power_off_via_menu(self):
        menu = MenuController()
        menu.button_press()          # open (sel=0 = Power)
        menu.button_press()          # execute → Power OFF
        assert menu.system_on  is False
        assert menu.menu_state == "closed"

    def test_button_while_off_wakes_and_shows_menu(self):
        menu = MenuController()
        menu.button_press(); menu.button_press()   # open → power off
        menu.button_press()                        # wake
        assert menu.system_on  is True
        assert menu.menu_state == "main"
        assert menu.main_sel   == 0

    def test_power_on_via_menu_after_wake(self):
        menu = MenuController()
        menu.button_press(); menu.button_press()   # power off
        menu.button_press()                        # wake → menu open (sel=0=Power)
        menu.button_press()                        # execute → Power OFF again (toggle)
        assert menu.system_on is False

    # ── Demo mode option ──────────────────────────────────────────────────

    def test_demo_mode_toggle_on(self):
        menu = MenuController()
        menu.button_press()          # open
        menu.rotate(1)               # sel=1 (Demo mode)
        menu.button_press()          # execute
        assert menu.demo_mode  is True
        assert menu.menu_state == "closed"

    def test_demo_mode_toggle_off(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(1); menu.button_press()  # demo ON
        menu.button_press(); menu.rotate(1); menu.button_press()  # demo OFF
        assert menu.demo_mode is False

    def test_demo_mode_independent_of_power(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(1); menu.button_press()   # demo ON
        assert menu.system_on is True                              # power unchanged
        menu.button_press()                                        # open again
        menu.button_press()                                        # power off
        assert menu.demo_mode is True                              # demo unchanged

    # ── Settings option ───────────────────────────────────────────────────

    def test_settings_opens_submenu(self):
        menu = MenuController()
        menu.button_press()          # open
        menu.rotate(2)               # sel=2 (Settings)
        menu.button_press()          # enter settings
        assert menu.menu_state   == "settings"
        assert menu.settings_sel == 0

    def test_settings_rotate_scrolls(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()  # enter settings
        menu.rotate(1)
        assert menu.settings_sel == 1

    def test_settings_rotate_wraps(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()
        # wrap past "Back" (index NUM_SETTINGS) back to 0
        for _ in range(MenuController.NUM_SETTINGS + 1):
            menu.rotate(1)
        assert menu.settings_sel == 0

    def test_settings_back_returns_to_main(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()  # enter settings
        # navigate to Back (last item)
        for _ in range(MenuController.NUM_SETTINGS):
            menu.rotate(1)
        assert menu.settings_sel == MenuController.NUM_SETTINGS
        menu.button_press()          # execute Back
        assert menu.menu_state == "main"

    def test_settings_edit_on_press(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()  # enter settings
        menu.button_press()          # edit first item (TPS High)
        assert menu.menu_state == "edit"

    def test_settings_edit_rotate_changes_value(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()  # settings
        menu.button_press()          # edit TPS High (default 60, step 5)
        menu.rotate(1)
        assert menu.tps_high == 65.0

    def test_settings_edit_rotate_clamps_max(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()
        menu.button_press()          # edit TPS High (max 100)
        for _ in range(20):
            menu.rotate(1)
        assert menu.tps_high == 100.0

    def test_settings_edit_rotate_clamps_min(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()
        menu.button_press()          # edit TPS High (min 10)
        for _ in range(20):
            menu.rotate(-1)
        assert menu.tps_high == 10.0

    def test_settings_edit_confirm_returns_to_list(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(2); menu.button_press()
        menu.button_press()          # enter edit
        menu.rotate(1)               # change value
        menu.button_press()          # confirm
        assert menu.menu_state == "settings"

    # ── Exit option ───────────────────────────────────────────────────────

    def test_exit_closes_menu(self):
        menu = MenuController()
        menu.button_press()          # open
        menu.rotate(3)               # sel=3 (Exit)
        menu.button_press()          # execute
        assert menu.menu_state == "closed"

    def test_exit_leaves_state_unchanged(self):
        menu = MenuController()
        menu.button_press(); menu.rotate(1); menu.button_press()   # demo ON
        menu.button_press()                                         # open again
        menu.rotate(3)                                              # Exit
        menu.button_press()
        assert menu.demo_mode  is True   # unchanged
        assert menu.system_on  is True   # unchanged


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

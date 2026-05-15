"""
Python mirror of the C++ OBD logic in the Arduino sketch.

Core logic lives in lib/turbo_logic.py (shared with the recording viewer).
This module re-exports everything tests need and adds MenuController,
which is test-specific and not needed by the viewer.

When you change a threshold or algorithm in the sketch, update lib/turbo_logic.py.
Constants are loaded from Config.h automatically — no manual sync needed.
"""

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'lib'))

from turbo_logic import (  # noqa: F401 — re-exported for test imports
    TURBO_THROTTLE_HIGH,
    TURBO_THROTTLE_LOW,
    TURBO_RPM_MIN,
    TURBO_MIN_GEAR,
    TURBO_MAX_GEAR,
    TURBO_COOLDOWN_MS,
    TURBO_SPEED_GEAR12,
    TURBO_SPEED_GEAR23,
    parse_pid,
    estimate_gear,
    TurboTrigger,
)


class MenuController:
    """
    Mirror of the C++ menu state machine in turbo.ino.

    States: closed → main → settings → edit (and back).
    Call button_press() for encoder-button clicks, rotate(delta) for turns.
    """

    # Main menu item indices
    MAIN_POWER    = 0
    MAIN_DEMO     = 1
    MAIN_SETTINGS = 2
    MAIN_EXIT     = 3
    NUM_MAIN      = 4

    # Settings: (label, attr_name, step, vmin, vmax)
    CFG_DEFS = [
        ("TPS High %",  "tps_high",    5.0,  10.0, 100.0),
        ("TPS Low  %",  "tps_low",     1.0,   0.0,  50.0),
        ("RPM Min",     "rpm_min",   100.0, 500.0, 6000.0),
        ("Min Gear",    "min_gear",    1.0,   1.0,    6.0),
        ("Max Gear",    "max_gear",    1.0,   1.0,    6.0),
        ("Cooldown ms", "cooldown_ms",100.0, 500.0,10000.0),
        ("Vol Gear 1",  "vol_gear1",   1.0,   0.0,   30.0),
        ("Vol Gear 2",  "vol_gear2",   1.0,   0.0,   30.0),
        ("Spd G1/G2 km", "spd12",      5.0,   0.0,  100.0),
        ("Spd G2/G3 km", "spd23",      5.0,   0.0,  150.0),
        ("Vol Voice",   "vol_voice",   1.0,   0.0,   30.0),
    ]
    NUM_SETTINGS = len(CFG_DEFS)   # Back is index NUM_SETTINGS

    def __init__(self):
        self.menu_state   = "closed"   # closed | main | settings | edit
        self.main_sel     = 0
        self.settings_sel = 0
        self.system_on    = True
        self.demo_mode    = False
        # Settings values — defaults mirror C++ cfg* initial values
        self.tps_high    = TURBO_THROTTLE_HIGH
        self.tps_low     = TURBO_THROTTLE_LOW
        self.rpm_min     = TURBO_RPM_MIN
        self.min_gear    = float(TURBO_MIN_GEAR)
        self.max_gear    = float(TURBO_MAX_GEAR)
        self.cooldown_ms = float(TURBO_COOLDOWN_MS)
        self.vol_gear1   = 30.0
        self.vol_gear2   = 27.0
        self.spd12       = TURBO_SPEED_GEAR12
        self.spd23       = TURBO_SPEED_GEAR23
        self.vol_voice   = 13.0

    def button_press(self):
        """Simulate one encoder button press."""
        if not self.system_on:
            self.system_on  = True
            self.menu_state = "main"
            self.main_sel   = 0
        elif self.menu_state == "closed":
            self.menu_state = "main"
            self.main_sel   = 0
        elif self.menu_state == "main":
            self._execute_main()
        elif self.menu_state == "settings":
            self._execute_settings()
        elif self.menu_state == "edit":
            self.menu_state = "settings"   # confirm value

    def _execute_main(self):
        sel = self.main_sel
        if sel == self.MAIN_POWER:
            self.system_on = not self.system_on
            if not self.system_on:
                self.menu_state = "closed"
        elif sel == self.MAIN_DEMO:
            self.demo_mode = not self.demo_mode
            self.menu_state = "closed"
        elif sel == self.MAIN_SETTINGS:
            self.menu_state   = "settings"
            self.settings_sel = 0
        elif sel == self.MAIN_EXIT:
            self.menu_state = "closed"

    def _execute_settings(self):
        if self.settings_sel == self.NUM_SETTINGS:   # Back
            self.menu_state = "main"
        else:
            self.menu_state = "edit"

    def rotate(self, delta: int):
        """Simulate encoder rotation."""
        if self.menu_state == "main":
            self.main_sel = (self.main_sel + delta) % self.NUM_MAIN
        elif self.menu_state == "settings":
            total = self.NUM_SETTINGS + 1   # +1 for Back
            self.settings_sel = (self.settings_sel + delta) % total
        elif self.menu_state == "edit":
            _, attr, step, vmin, vmax = self.CFG_DEFS[self.settings_sel]
            new_val = getattr(self, attr) + delta * step
            setattr(self, attr, max(vmin, min(vmax, new_val)))


class TurboTrigger:
    """
    Mirror of C++ checkTurbo() — stateful Turbo trigger logic.

    Tracks previous TPS and cooldown timer. Call update() on every
    OBD poll cycle (every 100 ms). Returns True when Turbo should fire.

    Thresholds can be overridden for scenario tuning:
        trigger = TurboTrigger(throttle_high=35, rpm_min=1200)
    """

    def __init__(
        self,
        throttle_high: float = TURBO_THROTTLE_HIGH,
        throttle_low:  float = TURBO_THROTTLE_LOW,
        rpm_min:       float = TURBO_RPM_MIN,
        min_gear:      int   = TURBO_MIN_GEAR,
        max_gear:      int   = TURBO_MAX_GEAR,
        cooldown_ms:   float = TURBO_COOLDOWN_MS,
    ):
        self.throttle_high = throttle_high
        self.throttle_low  = throttle_low
        self.rpm_min       = rpm_min
        self.min_gear      = min_gear
        self.max_gear      = max_gear
        self.cooldown_ms   = cooldown_ms

        self._prev_tps    = 0.0
        self._last_turbo_ms = -(cooldown_ms + 1)  # allow immediate first trigger
        self.count        = 0
        self.events: list[dict] = []             # log of every trigger

    def update(self, tps: float, rpm: float, speed: float, now_ms: float) -> bool:
        """
        Process one poll cycle.
        Returns True if Turbo triggered this cycle.
        """
        gear = estimate_gear(rpm, speed)
        triggered = (
            now_ms - self._last_turbo_ms >= self.cooldown_ms
            and self._prev_tps > self.throttle_high
            and tps            < self.throttle_low
            and rpm            > self.rpm_min
            and self.min_gear <= gear <= self.max_gear
        )
        if triggered:
            self._last_turbo_ms = now_ms
            self.count += 1
            self.events.append({
                "time_ms":  now_ms,
                "prev_tps": self._prev_tps,
                "tps":      tps,
                "rpm":      rpm,
                "speed":    speed,
                "gear":     gear,
            })
        self._prev_tps = tps
        return triggered
